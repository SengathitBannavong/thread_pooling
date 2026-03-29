import pandas as pd
import matplotlib
matplotlib.use('Agg') # Use non-interactive backend
import matplotlib.pyplot as plt
import sys
import os

def plot_benchmark(csv_path, output_dir):
    # Read the CSV file
    try:
        df = pd.read_csv(csv_path)
    except Exception as e:
        print(f"Error reading CSV: {e}")
        return

    # Create plots directory if it doesn't exist
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # Base name for files
    base_name = os.path.basename(csv_path).replace('.txt', '').replace('.csv', '')
    
    # Determine X-axis
    is_scaling = "scaling" in base_name
    is_stability = "stability" in base_name
    
    x_col = 'run'
    if is_scaling:
        # For scaling, we want 'method' (which is 'N_workers') on X axis
        # Extract number from 'N_workers'
        df['workers'] = df['method'].str.extract(r'(\d+)').astype(int)
        df = df.sort_values('workers')
        x_col = 'workers'
    elif is_stability:
        x_col = 'run'
    
    # 1. Throughput Comparison
    plt.figure(figsize=(10, 6))
    if is_scaling:
        plt.plot(df[x_col], df['throughput'], marker='o')
        plt.xlabel('Number of Workers')
    else:
        for method in df['method'].unique():
            method_data = df[df['method'] == method]
            plt.plot(method_data[x_col], method_data['throughput'], marker='o', label=method)
        plt.xlabel('Run / Cycle Number')
        plt.legend()
    
    plt.title(f'Throughput - {base_name}')
    plt.ylabel('Throughput (Tasks/sec)')
    plt.grid(True)
    plt.savefig(os.path.join(output_dir, f'throughput_{base_name}.png'))
    plt.close()

    # 2. Average Latency Comparison
    plt.figure(figsize=(10, 6))
    if is_scaling:
        plt.plot(df[x_col], df['avg_latency_us'], marker='o', color='orange')
        plt.xlabel('Number of Workers')
    else:
        for method in df['method'].unique():
            method_data = df[df['method'] == method]
            plt.plot(method_data[x_col], method_data['avg_latency_us'], marker='o', label=method)
        plt.xlabel('Run / Cycle Number')
        plt.legend()
    
    plt.title(f'Average Latency - {base_name}')
    plt.ylabel('Latency (us)')
    plt.grid(True)
    plt.savefig(os.path.join(output_dir, f'latency_{base_name}.png'))
    plt.close()

    # 3. Bar Chart for Averages (if not stability/scaling)
    if not is_stability and not is_scaling:
        plt.figure(figsize=(10, 6))
        avg_throughput = df.groupby('method')['throughput'].mean()
        avg_throughput.plot(kind='bar')
        plt.title(f'Average Throughput - {base_name}')
        plt.ylabel('Throughput (Tasks/sec)')
        plt.xticks(rotation=45)
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, f'avg_throughput_{base_name}.png'))
        plt.close()

    print(f"Plots generated for {base_name} in {output_dir}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python benchmark/plot/plot.py <csv_file_path>")
    else:
        plot_benchmark(sys.argv[1], "benchmark/plot/")
