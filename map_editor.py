import tkinter as tk
import math
from dataclasses import dataclass, field

# ==========================================
# Configuration
# ==========================================

WINDOW_WIDTH = 800
WINDOW_HEIGHT = 600
GRID_SIZE = 25


# ==========================================
# Data Structures
# ==========================================

@dataclass
class WallGroup:
    # Grid coordinates, not pixel coordinates
    points: list[tuple[int, int]] = field(default_factory=list)

    # Tkinter Canvas IDs
    point_ids: list[int] = field(default_factory=list)
    wall_ids: list[int] = field(default_factory=list)


# ==========================================
# Map Editor
# ==========================================

class MapEditor:

    def __init__(self, root):
        self.root = root
        self.root.title("DOOM Map Editor")
        self.root.geometry(f"{WINDOW_WIDTH}x{WINDOW_HEIGHT}")

        # All wall groups
        self.groups: list[WallGroup] = []

        # Group currently being drawn
        self.current_group: WallGroup | None = None
        # Player information
        self.player_position = (15, 15)
        self.player_angle = 0

        # Canvas IDs for player
        self.player_id = None
        self.direction_id = None

        # Editing mode: "wall" or "player"
        self.mode = "wall"

        # Create canvas
        self.canvas = tk.Canvas(
            root,
            width=WINDOW_WIDTH,
            height=WINDOW_HEIGHT,
            bg="white"
        )
        self.canvas.pack(fill="both", expand=True)

        self.draw_grid()
        self.draw_player()

        # Mouse controls
        self.canvas.bind("<Button-1>", self.on_click)
        self.canvas.bind("<Button-2>", self.stop_drawing)
        self.canvas.bind("<Button-3>", self.stop_drawing)
        self.canvas.bind("<Control-Button-1>", self.stop_drawing)

        # Keyboard controls
        self.root.bind("<Command-z>", self.undo)
        # Player movement controls
        self.root.bind("<Up>", self.move_player)
        self.root.bind("<Down>", self.move_player)
        self.root.bind("<Left>", self.move_player)
        self.root.bind("<Right>", self.move_player)


    # ======================================
    # Draw Grid
    # ======================================

    def draw_grid(self):

        for x in range(0, WINDOW_WIDTH, GRID_SIZE):
            self.canvas.create_line(
                x, 0, x, WINDOW_HEIGHT,
                fill="lightgray"
            )

        for y in range(0, WINDOW_HEIGHT, GRID_SIZE):
            self.canvas.create_line(
                0, y, WINDOW_WIDTH, y,
                fill="lightgray"
            )


    # ======================================
    # Snap Mouse to Grid
    # ======================================

    def snap_to_grid(self, x, y):

        grid_x = round(x / GRID_SIZE)
        grid_y = round(y / GRID_SIZE)

        # Keep points inside the visible grid
        grid_x = max(0, min(grid_x, WINDOW_WIDTH // GRID_SIZE - 1))
        grid_y = max(0, min(grid_y, WINDOW_HEIGHT // GRID_SIZE - 1))

        return grid_x, grid_y


    # ======================================
    # Draw Point
    # ======================================

    def draw_point(self, x, y):

        pixel_x = x * GRID_SIZE
        pixel_y = y * GRID_SIZE

        radius = 4

        return self.canvas.create_oval(
            pixel_x - radius,
            pixel_y - radius,
            pixel_x + radius,
            pixel_y + radius,
            fill="red",
            outline="red"
        )

    # ======================================
# Player Movement
# ======================================

    def move_player(self, event):

        x, y = self.player_position

        if event.keysym == "Up":
            y -= 1

        elif event.keysym == "Down":
            y += 1

        elif event.keysym == "Left":
            x -= 1

        elif event.keysym == "Right":
            x += 1

        # Keep player inside the map
        x = max(0, min(x, WINDOW_WIDTH // GRID_SIZE - 1))
        y = max(0, min(y, WINDOW_HEIGHT // GRID_SIZE - 1))

        # Update player position
        self.player_position = (x, y)

        # Redraw player
        self.draw_player()

        print(f"Player position: {self.player_position}")
        # ======================================
        # Draw Wall
        # ======================================

    def draw_wall(self, start, end):

        x0, y0 = start
        x1, y1 = end

        return self.canvas.create_line(
            x0 * GRID_SIZE,
            y0 * GRID_SIZE,
            x1 * GRID_SIZE,
            y1 * GRID_SIZE,
            fill="black",
            width=3
        )

    # ======================================
    # Draw Player
    # ======================================

    def draw_player(self):
        # Remove old player graphics
        if self.player_id is not None:
            self.canvas.delete(self.player_id)

        if self.direction_id is not None:
            self.canvas.delete(self.direction_id)

        # Convert grid coordinates to pixels
        x, y = self.player_position
        px = x * GRID_SIZE
        py = y * GRID_SIZE

        # Draw blue player dot
        radius = 6
        self.player_id = self.canvas.create_oval(
            px - radius,
            py - radius,
            px + radius,
            py + radius,
            fill="blue",
            outline="white",
            width=2
        )

        # Draw direction arrow
        angle = math.radians(self.player_angle)
        length = 25

        end_x = px + math.sin(angle) * length
        end_y = py - math.cos(angle) * length

        self.direction_id = self.canvas.create_line(
            px, py,
            end_x, end_y,
            fill="blue",
            width=3,
            arrow=tk.LAST
        )

        # Keep player visible above walls
        self.canvas.tag_raise(self.player_id)
        self.canvas.tag_raise(self.direction_id)
    # ======================================
    # Left Click: Add Point / Wall
    # ======================================

    def on_click(self, event):

        # Ignore Control + left click
        if event.state & 0x0004:
            return

        point = self.snap_to_grid(event.x, event.y)

        # Start a new group
        if self.current_group is None:

            group = WallGroup()

            self.groups.append(group)
            self.current_group = group

        else:
            group = self.current_group

        # Prevent duplicate consecutive points
        if group.points and group.points[-1] == point:
            return

        # Draw wall from previous point
        if group.points:

            previous_point = group.points[-1]

            wall_id = self.draw_wall(previous_point, point)

            group.wall_ids.append(wall_id)

        # Draw and store point
        point_id = self.draw_point(*point)

        group.points.append(point)
        group.point_ids.append(point_id)

        print(f"Point added: {point}")


    # ======================================
    # Right Click: End Wall Group
    # ======================================

    def stop_drawing(self, event=None):

        self.current_group = None

        print("Wall group ended")


    # ======================================
    # Command + Z: Undo
    # ======================================

    def undo(self, event=None):

        if not self.groups:
            print("Nothing to undo")
            return

        # Get most recent group
        group = self.groups[-1]

        # Delete last point
        if group.point_ids:

            point_id = group.point_ids.pop()
            self.canvas.delete(point_id)

            group.points.pop()

        # Delete last wall
        if group.wall_ids:

            wall_id = group.wall_ids.pop()
            self.canvas.delete(wall_id)

        # Remove group if empty
        if not group.points:

            self.groups.pop()
            self.current_group = None

            print("Wall group removed")

        else:
            # Continue drawing from the last remaining point
            self.current_group = group

            print("Undo completed")


# ==========================================
# Main
# ==========================================

if __name__ == "__main__":

    root = tk.Tk()

    editor = MapEditor(root)

    root.mainloop()