import datetime as dt
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
from matplotlib.collections import PolyCollection
import numpy as np

filelist = [
    "audio",
    "hologram",
]
data = []
components = {}
comp_tag = 1
colormapping = {}
for file in filelist:
    with open(file, 'r') as timelineFile:
        name = file
        line = timelineFile.readline()
        while line:
            start, end = line.split(',')
            data.append((name, float(start), float(end)))
            if name not in components:
                components[name] = comp_tag
                colormapping[name] = 'C'+str(comp_tag)
                comp_tag += 1
            line = timelineFile.readline()


verts = []
colors = []
for d in data:
    v =  [(d[1], components[d[0]]-.4),
          (d[1], components[d[0]]+.4),
          (d[2], components[d[0]]+.4),
          (d[2], components[d[0]]-.4),
          (d[1], components[d[0]]-.4)]
    verts.append(v)
    colors.append(colormapping[d[0]])

bars = PolyCollection(verts, facecolors=colors)
fig, ax = plt.subplots()
ax.add_collection(bars)
ax.autoscale()
#loc = mdates.MinuteLocator(byminute=[0,15,30,45])
#ax.xaxis.set_major_locator(loc)
#ax.xaxis.set_major_formatter(mdates.AutoDateFormatter(loc))

ax.set_yticks(range(1, comp_tag, 1))
ax.set_yticklabels(components.keys())

# for ddl in np.arange(0, 3*1000.0, 1024.0/48000.0*1000.0):
#   ax.axvline(ddl, color='grey')

plt.show()