import svgwrite
import re

infile = "timeline.log"
outfile = "timeline.svg"

hpos = 100
hsec = 600
vsize = 30
vsep = 15

class Timeline:
    def __init__(self, name):
        self.name = name
        self.rangestart = None
        self.ranges = []

    def begin(self, time):
        self.rangestart = time

    def end(self, time):
        if self.rangestart == None:
            print("Warning: "+self.name+" got end without a begin")
            return
        self.ranges.append((self.rangestart, time))
        self.rangestart = None

    def draw(self, svg, idx, starttime, endtime):
        sy = idx * (vsize + vsep)
        ey = sy + vsize

        col = svgwrite.rgb(60, 84, 242, 'rgb')

        svg.add(svg.text(
            text=self.name,
            insert=(10, sy + vsize / 1.5 + vsep / 2),
            fill=col))

        for r in self.ranges:
            sx = (r[0] - starttime) * hsec + hpos
            ex = (r[1] - starttime) * hsec + hpos

            svg.add(svg.rect(
                insert=(sx, sy + vsep / 2),
                size=(ex - sx, ey - sy),
                fill=col))

        if self.rangestart is not None:
            sx = (self.rangestart - starttime) * hsec + hpos
            ex = (endtime - starttime) * hsec + hpos

            svg.add(svg.rect(
                insert=(sx, sy + vsep / 2),
                size=(ex - sx, ey - sy),
                fill=col))

starttime = None
endtime = None
timelines = []
timelinesMap = {}

rx = re.compile(r"(.*)?:\s*(.*)?:\s*(.*)")
for line in open(infile):
    if not line.endswith("\n"):
        continue

    match = rx.match(line)
    name = match.group(1)
    kind = match.group(2)
    time = float(match.group(3))

    if starttime == None or time < starttime:
        starttime = time
    if endtime == None or time > endtime:
        endtime = time

    if kind == "REGISTER":
        timelinesMap[name] = Timeline(name)
        timelines.append(timelinesMap[name])
    elif kind == "BEGIN":
        if timelinesMap[name] is None:
            print("Warning: "+name+" got BEGIN without REGISTER")
            continue
        timelinesMap[name].begin(time)
    elif kind == "END":
        if timelinesMap[name] is None:
            print("Warning: "+name+" got END without REGISTER")
            continue
        timelinesMap[name].end(time)

svg = svgwrite.Drawing(
    filename=outfile,
    viewBox=("0 0 {} {}".format(
        hpos + hsec * (endtime - starttime),
        len(timelines) * (vsize + vsep) + vsep)))

# Draw time lines
idx = 0
for timeline in timelines:
    timeline.draw(svg, idx, starttime, endtime)

    # Separator line
    if idx != 0:
        svg.add(svg.line(
            start=(0, idx * (vsize + vsep)),
            end=(hpos + (endtime - starttime) * hsec, idx * (vsize + vsep)),
            stroke=svgwrite.rgb(0, 0, 0, 'rgb')))

    idx += 1

# Draw ticks at 1/30 sec boundaries
pos = starttime
while pos < endtime:
    sx = (pos - starttime) * hsec + hpos
    h = idx * (vsize + vsep)
    svg.add(svg.line(
        start=(sx, 0),
        end=(sx, h),
        stroke=svgwrite.rgb(100, 100, 100, 'rgb')))

    pos += 1 / 30

svg.save()
print("Saved "+outfile+", timeline over "+str((endtime - starttime))+"s")
