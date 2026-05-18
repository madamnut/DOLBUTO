import bisect
import json
import struct
import sys
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox

try:
    from tkinterdnd2 import DND_FILES, TkinterDnD
except ImportError:
    DND_FILES = None
    TkinterDnD = None


DEFAULT_X_MIN = -2.0
DEFAULT_X_MAX = 2.0
DEFAULT_Y_MIN = 0.0
DEFAULT_Y_MAX = 512.0
DEFAULT_Y_LEFT = 120.0
DEFAULT_Y_MID = 130.0
DEFAULT_Y_RIGHT = 140.0
SAMPLE_COUNT = 1024
SPLINE_MAGIC = b"DLSF"
VERSION = 1


class SplineEditor:
    def __init__(self, root):
        self.root = root
        self.root.title("DOLBUTO Spline Editor")
        self.root.minsize(960, 700)

        self.output_path = Path(__file__).with_name("spline_lut.bin")
        self.curve_path = Path(__file__).with_name("spline_curve.json")
        self.current_curve_path = None
        self.x_min = DEFAULT_X_MIN
        self.x_max = DEFAULT_X_MAX
        self.y_min = DEFAULT_Y_MIN
        self.y_max = DEFAULT_Y_MAX
        self.points = [
            [self.x_min, DEFAULT_Y_LEFT],
            [0.0, DEFAULT_Y_MID],
            [self.x_max, DEFAULT_Y_RIGHT],
        ]
        self.drag_index = None
        self.drag_point = None
        self.drag_undo_pushed = False
        self.undo_stack = []
        self.max_undo = 100

        self.canvas_width = 900
        self.canvas_height = 500
        self.margin_left = 72
        self.margin_right = 24
        self.margin_top = 24
        self.margin_bottom = 56

        ranges = tk.Frame(root)
        ranges.pack(fill=tk.X, padx=12, pady=(12, 6))

        self.x_min_var = tk.StringVar(value=self.format_value(self.x_min))
        self.x_max_var = tk.StringVar(value=self.format_value(self.x_max))
        self.y_min_var = tk.StringVar(value=self.format_value(self.y_min))
        self.y_max_var = tk.StringVar(value=self.format_value(self.y_max))

        tk.Label(ranges, text="X Min").pack(side=tk.LEFT)
        tk.Entry(ranges, textvariable=self.x_min_var, width=10).pack(side=tk.LEFT, padx=(4, 12))
        tk.Label(ranges, text="X Max").pack(side=tk.LEFT)
        tk.Entry(ranges, textvariable=self.x_max_var, width=10).pack(side=tk.LEFT, padx=(4, 12))
        tk.Label(ranges, text="Y Min").pack(side=tk.LEFT)
        tk.Entry(ranges, textvariable=self.y_min_var, width=10).pack(side=tk.LEFT, padx=(4, 12))
        tk.Label(ranges, text="Y Max").pack(side=tk.LEFT)
        tk.Entry(ranges, textvariable=self.y_max_var, width=10).pack(side=tk.LEFT, padx=(4, 12))
        tk.Button(ranges, text="Apply Range", command=self.apply_range).pack(side=tk.LEFT)

        self.canvas = tk.Canvas(root, width=self.canvas_width, height=self.canvas_height, background="#151515")
        self.canvas.pack(fill=tk.BOTH, expand=True, padx=12, pady=(0, 6))

        controls = tk.Frame(root)
        controls.pack(fill=tk.X, padx=12, pady=(0, 12))

        self.status = tk.StringVar()
        tk.Button(controls, text="Export LUT", command=self.export_lut).pack(side=tk.LEFT)
        tk.Button(controls, text="Save JSON", command=self.save_curve).pack(side=tk.LEFT, padx=(8, 0))
        tk.Button(controls, text="Load JSON", command=self.load_curve).pack(side=tk.LEFT, padx=(8, 0))
        tk.Button(controls, text="Reset", command=self.reset_curve).pack(side=tk.LEFT, padx=(8, 0))
        tk.Label(controls, textvariable=self.status, anchor="w").pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(16, 0))

        self.canvas.bind("<Configure>", self.on_resize)
        self.canvas.bind("<Button-1>", self.on_left_down)
        self.canvas.bind("<B1-Motion>", self.on_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_left_up)
        self.canvas.bind("<Double-Button-1>", self.on_double_click)
        self.canvas.bind("<Button-3>", self.on_right_down)
        self.root.bind("<Control-z>", self.undo)
        self.root.bind("<Control-Z>", self.undo)

        self.enable_file_drop()
        self.draw()

    def format_value(self, value):
        if abs(value - round(value)) < 0.000001:
            return str(int(round(value)))
        return f"{value:.6f}".rstrip("0").rstrip(".")

    def graph_width(self):
        return max(1, self.canvas_width - self.margin_left - self.margin_right)

    def graph_height(self):
        return max(1, self.canvas_height - self.margin_top - self.margin_bottom)

    def value_to_x(self, value):
        t = (value - self.x_min) / (self.x_max - self.x_min)
        return self.margin_left + t * self.graph_width()

    def value_to_y(self, value):
        t = (value - self.y_min) / (self.y_max - self.y_min)
        return self.margin_top + (1.0 - t) * self.graph_height()

    def screen_to_x_value(self, x):
        t = (x - self.margin_left) / self.graph_width()
        return max(self.x_min, min(self.x_max, self.x_min + t * (self.x_max - self.x_min)))

    def screen_to_y_value(self, y):
        t = 1.0 - ((y - self.margin_top) / self.graph_height())
        return max(self.y_min, min(self.y_max, self.y_min + t * (self.y_max - self.y_min)))

    def sorted_points(self):
        self.points.sort(key=lambda item: item[0])
        return self.points

    def is_endpoint(self, point):
        return abs(point[0] - self.x_min) < 0.000001 or abs(point[0] - self.x_max) < 0.000001

    def clone_state(self):
        return {
            "x_min": self.x_min,
            "x_max": self.x_max,
            "y_min": self.y_min,
            "y_max": self.y_max,
            "points": [[point[0], point[1]] for point in self.points],
        }

    def restore_state(self, state):
        self.x_min = float(state["x_min"])
        self.x_max = float(state["x_max"])
        self.y_min = float(state["y_min"])
        self.y_max = float(state["y_max"])
        self.points = [[float(x), float(y)] for x, y in state["points"]]
        self.sync_range_fields()
        self.sorted_points()

    def push_undo(self):
        self.undo_stack.append(self.clone_state())
        if len(self.undo_stack) > self.max_undo:
            del self.undo_stack[0]

    def undo(self, _event=None):
        if not self.undo_stack:
            return

        self.restore_state(self.undo_stack.pop())
        self.drag_index = None
        self.drag_point = None
        self.drag_undo_pushed = False
        self.draw()

    def sample_y(self, x_value):
        points = self.sorted_points()
        if x_value <= points[0][0]:
            return points[0][1]
        if x_value >= points[-1][0]:
            return points[-1][1]

        xs = [point[0] for point in points]
        index = bisect.bisect_right(xs, x_value)
        left = points[index - 1]
        right = points[index]
        span = right[0] - left[0]
        if span <= 0.0:
            return left[1]

        t = (x_value - left[0]) / span
        return left[1] + (right[1] - left[1]) * t

    def build_samples(self):
        samples = []
        for i in range(SAMPLE_COUNT):
            t = i / (SAMPLE_COUNT - 1)
            x_value = self.x_min + t * (self.x_max - self.x_min)
            samples.append(max(self.y_min, min(self.y_max, self.sample_y(x_value))))
        return samples

    def nearest_point(self, x, y, radius=10):
        nearest = None
        nearest_distance = radius * radius
        for index, point in enumerate(self.points):
            px = self.value_to_x(point[0])
            py = self.value_to_y(point[1])
            distance = (px - x) * (px - x) + (py - y) * (py - y)
            if distance <= nearest_distance:
                nearest = index
                nearest_distance = distance
        return nearest

    def sync_range_fields(self):
        self.x_min_var.set(self.format_value(self.x_min))
        self.x_max_var.set(self.format_value(self.x_max))
        self.y_min_var.set(self.format_value(self.y_min))
        self.y_max_var.set(self.format_value(self.y_max))

    def clamp_points_to_range(self):
        self.sorted_points()
        if len(self.points) < 2:
            self.points = [[self.x_min, self.y_min], [self.x_max, self.y_max]]
            return

        span = self.x_max - self.x_min
        epsilon = span * 0.000001
        self.points[0][0] = self.x_min
        self.points[-1][0] = self.x_max
        for index, point in enumerate(self.points):
            point[1] = max(self.y_min, min(self.y_max, point[1]))
            if index != 0 and index != len(self.points) - 1:
                point[0] = max(self.x_min + epsilon, min(self.x_max - epsilon, point[0]))
        self.sorted_points()

    def apply_range(self):
        try:
            x_min = float(self.x_min_var.get())
            x_max = float(self.x_max_var.get())
            y_min = float(self.y_min_var.get())
            y_max = float(self.y_max_var.get())
        except ValueError:
            messagebox.showerror("Apply Range", "Range values must be numeric.", parent=self.root)
            return

        if x_min >= x_max:
            messagebox.showerror("Apply Range", "X Min must be less than X Max.", parent=self.root)
            return
        if y_min >= y_max:
            messagebox.showerror("Apply Range", "Y Min must be less than Y Max.", parent=self.root)
            return

        self.push_undo()
        self.x_min = x_min
        self.x_max = x_max
        self.y_min = y_min
        self.y_max = y_max
        self.clamp_points_to_range()
        self.sync_range_fields()
        self.draw()

    def on_resize(self, event):
        self.canvas_width = event.width
        self.canvas_height = event.height
        self.draw()

    def on_left_down(self, event):
        self.drag_index = self.nearest_point(event.x, event.y)
        self.drag_point = self.points[self.drag_index] if self.drag_index is not None else None
        self.drag_undo_pushed = False

    def on_drag(self, event):
        if self.drag_point is None:
            return

        if not self.drag_undo_pushed:
            self.push_undo()
            self.drag_undo_pushed = True

        point = self.drag_point
        if not self.is_endpoint(point):
            point[0] = self.screen_to_x_value(event.x)
        point[1] = self.screen_to_y_value(event.y)
        self.draw()

    def on_left_up(self, _event):
        self.drag_index = None
        self.drag_point = None
        self.drag_undo_pushed = False
        self.clamp_points_to_range()
        self.draw()

    def on_double_click(self, event):
        x_value = self.screen_to_x_value(event.x)
        y_value = self.screen_to_y_value(event.y)
        if x_value <= self.x_min or x_value >= self.x_max:
            return
        self.push_undo()
        self.points.append([x_value, y_value])
        self.sorted_points()
        self.draw()

    def on_right_down(self, event):
        index = self.nearest_point(event.x, event.y)
        if index is None:
            return
        self.open_point_editor(index, event.x_root, event.y_root)

    def open_point_editor(self, index, screen_x, screen_y):
        if index < 0 or index >= len(self.points):
            return

        point = self.points[index]
        is_endpoint = self.is_endpoint(point)

        dialog = tk.Toplevel(self.root)
        dialog.title("Edit Point")
        dialog.resizable(False, False)
        dialog.transient(self.root)
        dialog.grab_set()

        x_var = tk.StringVar(value=self.format_value(point[0]))
        y_var = tk.StringVar(value=self.format_value(point[1]))

        tk.Label(dialog, text="X").grid(row=0, column=0, sticky="e", padx=(12, 8), pady=(12, 4))
        x_entry = tk.Entry(dialog, textvariable=x_var, width=16)
        x_entry.grid(row=0, column=1, columnspan=3, sticky="we", padx=(0, 12), pady=(12, 4))
        if is_endpoint:
            x_entry.configure(state="disabled")

        tk.Label(dialog, text="Y").grid(row=1, column=0, sticky="e", padx=(12, 8), pady=4)
        y_entry = tk.Entry(dialog, textvariable=y_var, width=16)
        y_entry.grid(row=1, column=1, columnspan=3, sticky="we", padx=(0, 12), pady=4)

        def apply_point():
            try:
                new_x = point[0] if is_endpoint else float(x_var.get())
                new_y = float(y_var.get())
            except ValueError:
                messagebox.showerror("Edit Point", "X and Y must be numeric.", parent=dialog)
                return

            if is_endpoint:
                new_x = point[0]
            elif new_x <= self.x_min or new_x >= self.x_max:
                messagebox.showerror("Edit Point", f"X must be greater than {self.format_value(self.x_min)} and less than {self.format_value(self.x_max)}.", parent=dialog)
                return

            if new_y < self.y_min or new_y > self.y_max:
                messagebox.showerror("Edit Point", f"Y must be between {self.format_value(self.y_min)} and {self.format_value(self.y_max)}.", parent=dialog)
                return

            self.push_undo()
            point[0] = new_x
            point[1] = new_y
            self.sorted_points()
            self.draw()
            dialog.destroy()

        def delete_point():
            if is_endpoint:
                return

            self.push_undo()
            if point in self.points:
                self.points.remove(point)
            self.draw()
            dialog.destroy()

        def cancel():
            dialog.destroy()

        button_row = tk.Frame(dialog)
        button_row.grid(row=2, column=0, columnspan=4, sticky="e", padx=12, pady=(8, 12))
        tk.Button(button_row, text="Apply", command=apply_point).pack(side=tk.LEFT)
        delete_button = tk.Button(button_row, text="Delete", command=delete_point)
        delete_button.pack(side=tk.LEFT, padx=(8, 0))
        if is_endpoint:
            delete_button.configure(state="disabled")
        tk.Button(button_row, text="Cancel", command=cancel).pack(side=tk.LEFT, padx=(8, 0))

        dialog.bind("<Return>", lambda _event: apply_point())
        dialog.bind("<Escape>", lambda _event: cancel())
        dialog.update_idletasks()

        width = dialog.winfo_reqwidth()
        height = dialog.winfo_reqheight()
        x = screen_x + 12
        y = screen_y + 12
        screen_width = dialog.winfo_screenwidth()
        screen_height = dialog.winfo_screenheight()
        x = max(0, min(x, screen_width - width - 12))
        y = max(0, min(y, screen_height - height - 48))
        dialog.geometry(f"+{x}+{y}")

        y_entry.focus_set()

    def tick_values(self, minimum, maximum, segments):
        if segments <= 0:
            return [minimum, maximum]
        return [minimum + (maximum - minimum) * (i / segments) for i in range(segments + 1)]

    def draw_axes(self):
        left = self.margin_left
        right = self.canvas_width - self.margin_right
        top = self.margin_top
        bottom = self.canvas_height - self.margin_bottom

        self.canvas.create_rectangle(left, top, right, bottom, outline="#444444")

        for y_value in self.tick_values(self.y_min, self.y_max, 8):
            y = self.value_to_y(y_value)
            color = "#3c3c3c" if abs(y_value) < 0.000001 else "#2a2a2a"
            self.canvas.create_line(left, y, right, y, fill=color)
            self.canvas.create_text(left - 10, y, text=self.format_value(y_value), fill="#cfcfcf", anchor="e", font=("Consolas", 9))

        for x_value in self.tick_values(self.x_min, self.x_max, 4):
            x = self.value_to_x(x_value)
            color = "#3c3c3c" if abs(x_value) < 0.000001 else "#2a2a2a"
            self.canvas.create_line(x, top, x, bottom, fill=color)
            self.canvas.create_text(x, bottom + 20, text=self.format_value(x_value), fill="#cfcfcf", anchor="n", font=("Consolas", 9))

        self.canvas.create_text((left + right) * 0.5, self.canvas_height - 18, text="X", fill="#e0e0e0", font=("Consolas", 10))
        self.canvas.create_text(18, (top + bottom) * 0.5, text="Y", fill="#e0e0e0", angle=90, font=("Consolas", 10))

    def draw_curve(self):
        samples = self.build_samples()
        previous = None
        for i, y_value in enumerate(samples):
            t = i / (SAMPLE_COUNT - 1)
            x_value = self.x_min + t * (self.x_max - self.x_min)
            current = (self.value_to_x(x_value), self.value_to_y(y_value))
            if previous is not None:
                self.canvas.create_line(previous[0], previous[1], current[0], current[1], fill="#78d6ff", width=2)
            previous = current

        for point in self.sorted_points():
            x = self.value_to_x(point[0])
            y = self.value_to_y(point[1])
            self.canvas.create_oval(x - 5, y - 5, x + 5, y + 5, fill="#ffcc66", outline="#101010")
            self.canvas.create_text(x, y - 14, text=f"{self.format_value(point[0])}, {self.format_value(point[1])}", fill="#ffffff", font=("Consolas", 8))

    def draw(self):
        self.canvas.delete("all")
        self.draw_axes()
        self.draw_curve()
        file_name = self.current_curve_path.name if self.current_curve_path else "new curve"
        self.status.set(f"{file_name} | Spline: {SAMPLE_COUNT} samples, X {self.format_value(self.x_min)}..{self.format_value(self.x_max)}, Y {self.format_value(self.y_min)}..{self.format_value(self.y_max)}")

    def curve_data(self):
        return {
            "editor": "DOLBUTO Spline Editor",
            "version": 1,
            "xMin": self.x_min,
            "xMax": self.x_max,
            "yMin": self.y_min,
            "yMax": self.y_max,
            "sampleCount": SAMPLE_COUNT,
            "points": self.sorted_points(),
        }

    def write_curve_json(self, path):
        with open(path, "w", encoding="utf-8") as file:
            json.dump(self.curve_data(), file, indent=2)

    def load_curve_from_path(self, path):
        path = Path(path)
        if path.suffix.lower() != ".json":
            messagebox.showerror("Load JSON", "Only .json curve files can be loaded.", parent=self.root)
            return

        try:
            with open(path, "r", encoding="utf-8") as file:
                data = json.load(file)
        except OSError as error:
            messagebox.showerror("Load JSON", f"Could not open curve:\n{error}", parent=self.root)
            return
        except json.JSONDecodeError as error:
            messagebox.showerror("Load JSON", f"Invalid JSON:\n{error}", parent=self.root)
            return

        points = data.get("points", [])
        if len(points) < 2:
            messagebox.showerror("Load JSON", "Curve must have at least two points.", parent=self.root)
            return

        try:
            x_min = float(data.get("xMin", data.get("noiseMin", DEFAULT_X_MIN)))
            x_max = float(data.get("xMax", data.get("noiseMax", DEFAULT_X_MAX)))
            y_min = float(data.get("yMin", data.get("heightMin", DEFAULT_Y_MIN)))
            y_max = float(data.get("yMax", data.get("heightMax", DEFAULT_Y_MAX)))
            loaded_points = [[float(x), float(y)] for x, y in points]
        except (TypeError, ValueError):
            messagebox.showerror("Load JSON", "Curve data contains non-numeric values.", parent=self.root)
            return

        if x_min >= x_max or y_min >= y_max:
            messagebox.showerror("Load JSON", "Curve range is invalid.", parent=self.root)
            return

        self.push_undo()
        self.x_min = x_min
        self.x_max = x_max
        self.y_min = y_min
        self.y_max = y_max
        self.points = loaded_points
        self.current_curve_path = path
        self.curve_path = path
        self.clamp_points_to_range()
        self.sync_range_fields()
        self.draw()

    def enable_file_drop(self):
        if DND_FILES is None:
            return

        for widget in (self.root, self.canvas):
            widget.drop_target_register(DND_FILES)
            widget.dnd_bind("<<Drop>>", self.on_file_drop)

    def on_file_drop(self, event):
        paths = self.root.tk.splitlist(event.data)
        json_paths = [path for path in paths if Path(path).suffix.lower() == ".json"]
        if json_paths:
            self.load_curve_from_path(json_paths[0])
        else:
            messagebox.showerror("Load JSON", "Drop a .json curve file.", parent=self.root)

    def export_lut(self):
        path = filedialog.asksaveasfilename(
            title="Export spline LUT",
            initialdir=str(Path(__file__).parent),
            initialfile=self.output_path.name,
            defaultextension=".bin",
            filetypes=[("Spline LUT", "*.bin"), ("All files", "*.*")]
        )
        if not path:
            return

        samples = self.build_samples()
        with open(path, "wb") as file:
            file.write(SPLINE_MAGIC)
            file.write(struct.pack("<IIffff", VERSION, SAMPLE_COUNT, self.x_min, self.x_max, self.y_min, self.y_max))
            file.write(struct.pack("<" + "f" * SAMPLE_COUNT, *samples))

        self.output_path = Path(path)
        messagebox.showinfo("Export LUT", f"Saved {SAMPLE_COUNT} float samples to:\n{path}")

    def save_curve(self):
        path = self.current_curve_path
        if path is None:
            selected_path = filedialog.asksaveasfilename(
                title="Save JSON",
                initialdir=str(Path(__file__).parent),
                initialfile=self.curve_path.name,
                defaultextension=".json",
                filetypes=[("Curve JSON", "*.json"), ("All files", "*.*")]
            )
            if not selected_path:
                return
            path = Path(selected_path)

        try:
            self.write_curve_json(path)
        except OSError as error:
            messagebox.showerror("Save JSON", f"Could not save curve:\n{error}", parent=self.root)
            return

        self.current_curve_path = path
        self.curve_path = path
        self.draw()

    def load_curve(self):
        path = filedialog.askopenfilename(
            title="Load JSON",
            initialdir=str(Path(__file__).parent),
            filetypes=[("Curve JSON", "*.json"), ("All files", "*.*")]
        )
        if not path:
            return
        self.load_curve_from_path(path)

    def reset_curve(self):
        self.push_undo()
        self.current_curve_path = None
        self.x_min = DEFAULT_X_MIN
        self.x_max = DEFAULT_X_MAX
        self.y_min = DEFAULT_Y_MIN
        self.y_max = DEFAULT_Y_MAX
        self.points = [
            [self.x_min, DEFAULT_Y_LEFT],
            [0.0, DEFAULT_Y_MID],
            [self.x_max, DEFAULT_Y_RIGHT],
        ]
        self.sync_range_fields()
        self.draw()


def main():
    root = TkinterDnD.Tk() if TkinterDnD is not None else tk.Tk()
    editor = SplineEditor(root)
    if len(sys.argv) > 1:
        root.after(0, lambda: editor.load_curve_from_path(sys.argv[1]))
    root.mainloop()


if __name__ == "__main__":
    main()
