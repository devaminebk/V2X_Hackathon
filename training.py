import pandas as pd
import numpy as np
from sklearn.cluster import KMeans
from sklearn.preprocessing import StandardScaler
import serial
import time

class TrafficRiskModel:
    def __init__(self):
        # Initialize our ML tools
        self.scaler = StandardScaler()
        self.kmeans = KMeans(n_clusters=3, random_state=42, n_init=10)
        
        # Placeholders for our trained data
        self.location_profiles = None
        self.risk_map = {}

    def train(self, csv_path):
        """Loads data, engineers features, and trains the K-Means model."""
        # 1. Load the fictional data
        df = pd.read_csv(csv_path)

        # 2. Feature Engineering
        self.location_profiles = df.groupby(['Location_ID', 'X', 'Y']).agg({
            'Speed': ['mean', 'std'],
            'Acceleration': ['mean', 'min'] 
        }).reset_index()

        self.location_profiles.columns = ['Location_ID', 'X', 'Y', 'Avg_Speed', 'Speed_Var', 'Avg_Accel', 'Max_Decel']
        
        # Fill NaNs
        self.location_profiles.fillna(0, inplace=True)

        # 3. Prepare Features
        features = ['Avg_Speed', 'Speed_Var', 'Max_Decel']
        X_train = self.location_profiles[features]
        X_scaled = self.scaler.fit_transform(X_train)

        # 4. Train the K-Means Model
        self.location_profiles['Cluster'] = self.kmeans.fit_predict(X_scaled)

        # 5. Automatically Rank Clusters by Risk
        cluster_summary = self.location_profiles.groupby('Cluster').agg({
            'Avg_Speed': 'mean',
            'Max_Decel': 'mean'
        })
        
        cluster_summary['Severity'] = cluster_summary['Avg_Speed'] + abs(cluster_summary['Max_Decel'])
        rank_map = cluster_summary['Severity'].sort_values().index

        # Map clusters to Risk Scores
        self.risk_map = {rank_map[0]: 25, rank_map[1]: 60, rank_map[2]: 90}
        self.location_profiles['Risk_Score'] = self.location_profiles['Cluster'].map(self.risk_map)
        
        print("✅ Model successfully trained and ready for inference.")

    def get_risk_by_coordinates(self, bus_x, bus_y):
        """Method 1: Returns the risk score of the closest historical location."""
        if self.location_profiles is None:
            raise ValueError("Model must be trained first. Call model.train('data.csv')")

        distances = np.sqrt((self.location_profiles['X'] - bus_x)**2 + (self.location_profiles['Y'] - bus_y)**2)
        nearest_idx = distances.idxmin()
        return self.location_profiles.loc[nearest_idx, 'Risk_Score']

# ==========================================
# --- SERIAL COMMUNICATION LOOP ---
# ==========================================
# ==========================================
# --- SERIAL COMMUNICATION LOOP ---
# ==========================================
if __name__ == "__main__":
    ai_system = TrafficRiskModel()
    ai_system.train('traffic_data.csv') 
    
    SERIAL_PORT = 'COM5' 
    BAUD_RATE = 115200
    
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        time.sleep(2)  # Give the ESP32 a moment to reset
        
        while True:
            if ser.in_waiting > 0:
                incoming_data = ser.readline().decode('utf-8').strip()
                
                if incoming_data:
                    try:
                        parts = incoming_data.split(',')
                        if len(parts) == 2:
                            bus_x = float(parts[0])
                            bus_y = float(parts[1])
                            
                            # Run the model and force it to be an integer
                            risk_score = int(ai_system.get_risk_by_coordinates(bus_x, bus_y))
                            
                            # Send strictly the integer and \n via serial
                            ser.write(f"{risk_score}\n".encode('utf-8'))
                            
                    except ValueError:
                        # Silently ignore any corrupted serial lines
                        pass
                        
    except serial.SerialException:
        pass
    except KeyboardInterrupt:
        if 'ser' in locals() and ser.is_open:
            ser.close()