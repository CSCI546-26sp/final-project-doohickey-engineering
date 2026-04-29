import matplotlib.pyplot as plt

runs = []
avg = []
minv = []
maxv = []

with open("latency_runs.txt") as f:
    for line in f:
        r, a, mn, mx = map(float, line.split())
        runs.append(r)
        avg.append(a)
        minv.append(mn)
        maxv.append(mx)

plt.figure(figsize=(8,6))

# Plot lines
plt.plot(runs, avg, marker='o', linewidth=2, label="Average")
plt.plot(runs, minv, marker='o', linestyle='--', label="Min")
plt.plot(runs, maxv, marker='o', linestyle='--', label="Max")

# Labels
plt.xlabel("Run Number")
plt.ylabel("Latency (ms)")
plt.title("Latency Across Runs (1000 Sequential Requests)")

plt.legend()
plt.grid(True)

plt.savefig("latency.png")
plt.show()