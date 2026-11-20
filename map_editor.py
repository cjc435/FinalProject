import tkinter as tk

# Window settings
WINDOW_WIDTH = 800
WINDOW_HEIGHT = 600
GRID_SIZE = 25

# Create main window
root = tk.Tk()
root.title("DOOM Map Editor")
root.geometry(f"{WINDOW_WIDTH}x{WINDOW_HEIGHT}")

# Create canvas
canvas = tk.Canvas(root, width=WINDOW_WIDTH, height=WINDOW_HEIGHT, bg="white")
canvas.pack(fill="both", expand=True)

# Draw vertical lines
for x in range(0, WINDOW_WIDTH, GRID_SIZE):
    canvas.create_line(x, 0, x, WINDOW_HEIGHT, fill="lightgray")

# Draw horizontal lines
for y in range(0, WINDOW_HEIGHT, GRID_SIZE):
    canvas.create_line(0, y, WINDOW_WIDTH, y, fill="lightgray")

# Store the starting point of the next wall
start_point = None

# Store all completed walls
walls = []
wall_ids = []

def stop_drawing(event):
    global start_point

    # Stop connecting new points to the previous wall
    start_point = None

    print("Wall chain ended")

# Handle mouse clicks
def on_click(event):
    global start_point
    # Ignore Control + left-click
    if event.state & 0x0004:
        return
    # Snap mouse position to grid
    x = round(event.x / GRID_SIZE) * GRID_SIZE
    y = round(event.y / GRID_SIZE) * GRID_SIZE

    # Draw a red dot
    radius = 4
    canvas.create_oval(
        x - radius,
        y - radius,
        x + radius,
        y + radius,
        fill="red"
    )

    if start_point is None:
        # First click: remember starting point
        start_point = (x, y)
        print(f"Wall start: {start_point}")

    else:
        # Second click: draw wall
        x0, y0 = start_point

        wall_id = canvas.create_line(
            x0, y0, x, y,
            fill="black",
            width=3
        )

        # Store wall coordinates
        walls.append(((x0, y0), (x, y)))

        # Store Canvas item ID separately
        wall_ids.append(wall_id)

        print(f"Wall created: {walls[-1]}")

        # Use the endpoint as the start of the next wall
        start_point = (x, y)


def undo_wall(event=None):
    global start_point

    if not wall_ids:
        print("No walls to undo")
        return

    # Remove last wall from the Canvas
    wall_id = wall_ids.pop()
    canvas.delete(wall_id)

    # Remove its coordinates
    walls.pop()

    # Stop drawing the current wall chain
    start_point = None

    print("Last wall removed")


# Connect mouse clicks to the function
canvas.bind("<Button-1>", on_click)

# Support right-click on different platforms
canvas.bind("<Button-2>", stop_drawing)
canvas.bind("<Button-3>", stop_drawing)

# Support Control + left-click on macOS
canvas.bind("<Control-Button-1>", stop_drawing)
root.bind("<Command-z>", undo_wall)
root.mainloop()