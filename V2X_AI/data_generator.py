import pandas as pd
import numpy as np
import random

# 1. Define 10 Locations with specific "Road Profiles"
# Profiles: (Mean Speed, Speed StdDev, Mean Accel, Accel StdDev)
profiles = {
    "Loc_0": (100, 10, 0, 2),    # Highway: High speed, steady
    "Loc_1": (25, 5, -2, 4),     # School Zone: Low speed, frequent braking
    "Loc_2": (45, 15, 0, 5),     # City Center: Stop-and-go
    "Loc_3": (80, 20, -3, 6),    # Highway Exit: High speed, sudden braking
    "Loc_4": (30, 8, -1, 2),     # Residential: Slow and steady
    "Loc_5": (60, 10, 1, 3),     # Main Road: Accelerating away from lights
    "Loc_6": (15, 3, -4, 5),     # Construction: Very slow, erratic braking
    "Loc_7": (110, 15, 0, 3),    # Rural Highway: Very high speed
    "Loc_8": (40, 12, -2, 4),    # Busy Intersection: Medium speed, braking
    "Loc_9": (50, 5, 0, 1),      # Suburban Loop: Constant medium speed
}

data = []

# 2. Generate coordinates and data points
for i in range(10):
    loc_id = f"Loc_{i}"
    # Assign a fixed (x, y) for this location
    x_coord = random.uniform(0, 100)
    y_coord = random.uniform(0, 100)
    
    # Each location gets 50 to 100 car samples
    num_samples = random.randint(50, 100)
    
    mean_spd, std_spd, mean_acc, std_acc = profiles[loc_id]
    
    for _ in range(num_samples):
        # Generate car data based on the location's profile
        speed = np.random.normal(mean_spd, std_spd)
        accel = np.random.normal(mean_acc, std_acc)
        
        # Ensure speed isn't negative
        speed = max(0, speed)
        
        data.append([loc_id, x_coord, y_coord, speed, accel])

# 3. Create DataFrame and Save
df = pd.DataFrame(data, columns=['Location_ID', 'X', 'Y', 'Speed', 'Acceleration'])

# Preview the data
print(df.head(10))
print(f"\nTotal data points generated: {len(df)}")

df.to_csv('traffic_data.csv', index=False)
print("\nSuccess: 'traffic_data.csv' has been created.")