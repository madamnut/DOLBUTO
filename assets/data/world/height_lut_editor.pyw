import bisect
import json
import struct
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox


NOISE_MIN = -2.0
NOISE_MAX = 2.0
LUT_COUNT = 1024
HEIGHT_MIN = 0
HEIGHT_MAX = 512
DEFAULT_HEIGHT_MIN = 120
DEFAULT_HEIGHT_MID = 130
DEFAULT_HEIGHT_MAX = 140
MAGIC = b"DLHT"
VERSION = 1


class HeightLutEditor:
    def __init__(self, root):
        self.root = root
        self.root.title("DOLBUTO Height LUT Editor")
        self.root.minsize(960, 640)

        self.output_path = Path(__file__).with_name("height_lut.bin")
        self.curve_path = Path(__file__).with_name("height_lut_curve.json")
        self.points = [
            [NOISE_MIN, DEFAULT_HEIGHT_MIN],
            [0.0, DEFAULT_HEIGHT_MID],
            [NOISE_MAX, DEFAULT_HEIGHT_MAX],
        ]
        self.drag_index = None

        self.canvas_width = 900
        self.canvas_height = 500
        self.margin_left = 64
        self.margin_right = 24
        self.margin_top = 24
        self.margin_bottom = 56

        self.canvas = tk.Canvas(root, width=self.canvas_width, height=self.canvas_height, background="#151515")
        self.canvas.pack(fill=tk.BOTH, expand=True, padx=12, pady=(12, 6))

        controls = tk.Frame(root)
        controls.pack(fill=tk.X, padx=12, pady=(0, 12))

        self.status = tk.StringVar()
        tk.Button(controls, text="Bake LUT", command=self.bake_lut).pack(side=tk.LEFT)
        tk.Button(controls, text="Save Curve", command=self.save_curve).pack(side=tk.LEFT, padx=(8, 0))
        tk.Button(controls, text="Load Curve", command=self.load_curve).pack(side=tk.LEFT, padx=(8, 0))
        tk.Button(controls, text="Reset", command=self.reset_curve).pack(side=tk.LEFT, padx=(8, 0))
        tk.Label(controls, textvariable=self.status, anchor="w").pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(16, 0))

        self.canvas.bind("<Configure>", self.on_resize)
        self.canvas.bind("<Button-1>", self.on_left_down)
        self.canvas.bind("<B1-Motion>", self.on_drag)
        self.canvas.bind("<ButtonRelease-1>", self.on_left_up)
        self.canvas.bind("<Double-Button-1>", self.on_double_click)
        self.canvas.bind("<Button-3>", self.on_right_down)

        self.draw()

    def graph_width(self):
        return max(1, self.canvas_width - self.margin_left - self.margin_right)

    def graph_height(self):
        return max(1, self.canvas_height - self.margin_top - self.margin_bottom)

    def noise_to_x(self, noise):
        t = (noise - NOISE_MIN) / (NOISE_MAX - NOISE_MIN)
        return self.margin_left + t * self.graph_width()

    def height_to_y(self, height):
        t = (height - HEIGHT_MIN) / (HEIGHT_MAX - HEIGHT_MIN)
        return self.margin_top + (1.0 - t) * self.graph_height()

    def x_to_noise(self, x):
        t = (x - self.margin_left) / self.graph_width()
        return max(NOISE_MIN, min(NOISE_MAX, NOISE_MIN + t * (NOISE_MAX - NOISE_MIN)))

    def y_to_height(self, y):
        t = 1.0 - ((y - self.margin_top) / self.graph_height())
        return int(round(max(HEIGHT_MIN, min(HEIGHT_MAX, HEIGHT_MIN + t * (HEIGHT_MAX - HEIGHT_MIN)))))

    def sorted_points(self):
        self.points.sort(key=lambda item: item[0])
        return self.points

    def sample_height(self, noise):
        points = self.sorted_points()
        if noise <= points[0][0]:
            return points[0][1]
        if noise >= points[-1][0]:
            return points[-1][1]

        xs = [point[0] for point in points]
        index = bisect.bisect_right(xs, noise)
        left = points[index - 1]
        right = points[index]
        span = right[0] - left[0]
        if span <= 0.0:
            return left[1]

        t = (noise - left[0]) / span
        return int(round(left[1] + (right[1] - left[1]) * t))

    def build_heights(self):
        heights = []
        for i in range(LUT_COUNT):
            t = i / (LUT_COUNT - 1)
            noise = NOISE_MIN + t * (NOISE_MAX - NOISE_MIN)
            heights.append(max(HEIGHT_MIN, min(HEIGHT_MAX, self.sample_height(noise))))
        return heights

    def nearest_point(self, x, y, radius=10):
        nearest = None
        nearest_distance = radius * radius
        for index, point in enumerate(self.points):
            px = self.noise_to_x(point[0])
            py = self.height_to_y(point[1])
            distance = (px - x) * (px - x) + (py - y) * (py - y)
            if distance <= nearest_distance:
                nearest = index
                nearest_distance = distance
        return nearest

    def on_resize(self, event):
        self.canvas_width = event.width
        self.canvas_height = event.height
        self.draw()

    def on_left_down(self, event):
        self.drag_index = self.nearest_point(event.x, event.y)

    def on_drag(self, event):
        if self.drag_index is None:
            return

        point = self.points[self.drag_index]
        is_endpoint = point[0] == NOISE_MIN or point[0] == NOISE_MAX
        if not is_endpoint:
            point[0] = self.x_to_noise(event.x)
        point[1] = self.y_to_height(event.y)
        self.draw()

    def on_left_up(self, _event):
        self.drag_index = None
        self.sorted_points()
        self.draw()

    def on_double_click(self, event):
        noise = self.x_to_noise(event.x)
        height = self.y_to_height(event.y)
        if noise <= NOISE_MIN or noise >= NOISE_MAX:
            return
        self.points.append([noise, height])
        self.sorted_points()
        self.draw()

    def on_right_down(self, event):
        index = self.nearest_point(event.x, event.y)
        if index is None:
            return
        if self.points[index][0] in (NOISE_MIN, NOISE_MAX):
            return
        del self.points[index]
        self.draw()

    def draw_axes(self):
        left = self.margin_left
        right = self.canvas_width - self.margin_right
        top = self.margin_top
        bottom = self.canvas_height - self.margin_bottom

        self.canvas.create_rectangle(left, top, right, bottom, outline="#444444")

        for height in range(0, HEIGHT_MAX + 1, 64):
            y = self.height_to_y(height)
            color = "#2a2a2a" if height not in (128, 140) else "#3c3c3c"
            self.canvas.create_line(left, y, right, y, fill=color)
            self.canvas.create_text(left - 10, y, text=str(height), fill="#cfcfcf", anchor="e", font=("Consolas", 9))

        for noise in (-2.0, -1.0, 0.0, 1.0, 2.0):
            x = self.noise_to_x(noise)
            self.canvas.create_line(x, top, x, bottom, fill="#2a2a2a")
            self.canvas.create_text(x, bottom + 20, text=f"{noise:.1f}", fill="#cfcfcf", anchor="n", font=("Consolas", 9))

        self.canvas.create_text((left + right) * 0.5, self.canvas_height - 18, text="Noise", fill="#e0e0e0", font=("Consolas", 10))
        self.canvas.create_text(18, (top + bottom) * 0.5, text="Height", fill="#e0e0e0", angle=90, font=("Consolas", 10))

    def draw_curve(self):
        heights = self.build_heights()
        previous = None
        for i, height in enumerate(heights):
            t = i / (LUT_COUNT - 1)
            noise = NOISE_MIN + t * (NOISE_MAX - NOISE_MIN)
            current = (self.noise_to_x(noise), self.height_to_y(height))
            if previous is not None:
                self.canvas.create_line(previous[0], previous[1], current[0], current[1], fill="#78d6ff", width=2)
            previous = current

        for point in self.sorted_points():
            x = self.noise_to_x(point[0])
            y = self.height_to_y(point[1])
            self.canvas.create_oval(x - 5, y - 5, x + 5, y + 5, fill="#ffcc66", outline="#101010")
            self.canvas.create_text(x, y - 14, text=f"{point[0]:.2f}, {point[1]}", fill="#ffffff", font=("Consolas", 8))

    def draw(self):
        self.canvas.delete("all")
        self.draw_axes()
        self.draw_curve()
        self.status.set(f"LUT: {LUT_COUNT} samples, noise {NOISE_MIN:.1f}..{NOISE_MAX:.1f}, output {self.output_path.name}")

    def bake_lut(self):
        path = filedialog.asksaveasfilename(
            title="Bake height LUT",
            initialdir=str(Path(__file__).parent),
            initialfile=self.output_path.name,
            defaultextension=".bin",
            filetypes=[("Height LUT", "*.bin"), ("All files", "*.*")]
        )
        if not path:
            return

        heights = self.build_heights()
        with open(path, "wb") as file:
            file.write(MAGIC)
            file.write(struct.pack("<IIff", VERSION, LUT_COUNT, NOISE_MIN, NOISE_MAX))
            file.write(struct.pack("<" + "H" * LUT_COUNT, *heights))

        messagebox.showinfo("Bake LUT", f"Saved {LUT_COUNT} heights to:\n{path}")

    def save_curve(self):
        path = filedialog.asksaveasfilename(
            title="Save curve",
            initialdir=str(Path(__file__).parent),
            initialfile=self.curve_path.name,
            defaultextension=".json",
            filetypes=[("Curve JSON", "*.json"), ("All files", "*.*")]
        )
        if not path:
            return

        data = {
            "noiseMin": NOISE_MIN,
            "noiseMax": NOISE_MAX,
            "lutCount": LUT_COUNT,
            "heightMin": HEIGHT_MIN,
            "heightMax": HEIGHT_MAX,
            "points": self.sorted_points(),
        }
        with open(path, "w", encoding="utf-8") as file:
            json.dump(data, file, indent=2)

    def load_curve(self):
        path = filedialog.askopenfilename(
            title="Load curve",
            initialdir=str(Path(__file__).parent),
            filetypes=[("Curve JSON", "*.json"), ("All files", "*.*")]
        )
        if not path:
            return

        with open(path, "r", encoding="utf-8") as file:
            data = json.load(file)

        points = data.get("points", [])
        if len(points) < 2:
            messagebox.showerror("Load curve", "Curve must have at least two points.")
            return

        self.points = [[float(noise), int(height)] for noise, height in points]
        self.points[0][0] = NOISE_MIN
        self.points[-1][0] = NOISE_MAX
        self.sorted_points()
        self.draw()

    def reset_curve(self):
        self.points = [
            [NOISE_MIN, DEFAULT_HEIGHT_MIN],
            [0.0, DEFAULT_HEIGHT_MID],
            [NOISE_MAX, DEFAULT_HEIGHT_MAX],
        ]
        self.draw()


def main():
    root = tk.Tk()
    HeightLutEditor(root)
    root.mainloop()


if __name__ == "__main__":
    main()
