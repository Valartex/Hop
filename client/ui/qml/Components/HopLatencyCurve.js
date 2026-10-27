.pragma library

// Монотонные кубические сегменты проходят через замеры, не добавляя выбросов.
function smooth(points) {
    if (points.length < 2) return []
    const slopes = [], tangents = [], result = []
    for (let i = 0; i < points.length - 1; ++i)
        slopes.push((points[i + 1].y - points[i].y) / (points[i + 1].x - points[i].x))
    tangents.push(slopes[0])
    for (let i = 1; i < points.length - 1; ++i)
        tangents.push(slopes[i - 1] * slopes[i] <= 0 ? 0 : (slopes[i - 1] + slopes[i]) / 2)
    tangents.push(slopes[slopes.length - 1])
    for (let i = 0; i < slopes.length; ++i) {
        if (slopes[i] === 0) { tangents[i] = 0; tangents[i + 1] = 0; continue }
        const a = tangents[i] / slopes[i], b = tangents[i + 1] / slopes[i]
        const norm = Math.sqrt(a * a + b * b)
        if (norm > 3) { tangents[i] *= 3 / norm; tangents[i + 1] *= 3 / norm }
    }
    for (let i = 0; i < slopes.length; ++i) {
        const first = points[i], last = points[i + 1], step = (last.x - first.x) / 3
        result.push({x1: first.x + step, y1: first.y + tangents[i] * step,
                     x2: last.x - step, y2: last.y - tangents[i + 1] * step, x: last.x, y: last.y})
    }
    return result
}

function scaleMaximum(maximum) {
    const rough = Math.max(1, maximum) * 1.05 / 4
    const magnitude = Math.pow(10, Math.floor(Math.log(rough) / Math.LN10))
    const normalized = rough / magnitude
    const step = (normalized <= 1 ? 1 : normalized <= 2 ? 2 : normalized <= 2.5 ? 2.5 : normalized <= 5 ? 5 : 10) * magnitude
    return Math.max(4, 4 * step)
}

function build(samples, left, top, width, height, maximum, windowStart, windowEnd) {
    const intervals = [], successful = []
    let peak = Math.max(0, maximum || 0)
    for (let i = 0; i < samples.length; ++i) {
        if (samples[i].ok) {
            successful.push(Number(samples[i].latencyMs))
            peak = Math.max(peak, Number(samples[i].maximum || 0), Number(samples[i].latencyMs))
        }
    }
    successful.sort(function(a, b) { return a - b })
    const typical = successful.length ? successful[Math.floor(successful.length / 2)] : 0
    const logarithmic = peak > 1000 && peak > Math.max(1, typical) * 6
    const ceiling = logarithmic ? Math.pow(10, Math.ceil(Math.log(peak) / Math.LN10)) : scaleMaximum(peak)
    function y(value) {
        const fraction = logarithmic ? Math.log(1 + Math.max(0, value)) / Math.log(1 + ceiling) : value / ceiling
        return top + height * (1 - fraction)
    }
    const ticks = []
    if (logarithmic) {
        ticks.push({value: 0, y: y(0)})
        for (let value = 10; value <= ceiling; value *= 10) ticks.push({value: value, y: y(value)})
    } else {
        for (let i = 0; i < 5; ++i) ticks.push({value: ceiling * i / 4, y: y(ceiling * i / 4)})
    }
    for (let i = 1; i < samples.length; ++i) {
        const interval = Number(samples[i].at) - Number(samples[i - 1].at)
        if (interval > 0) intervals.push(interval)
    }
    intervals.sort(function(a, b) { return a - b })
    const gap = intervals.length >= 3 ? intervals[Math.floor((intervals.length - 1) / 2)] * 4 : Infinity
    const first = samples.length ? Number(samples[0].at) : 0
    const last = samples.length ? Number(samples[samples.length - 1].at) : first
    const fixedWindow = Number(windowEnd) > Number(windowStart)
    const span = fixedWindow ? Number(windowEnd) - Number(windowStart) : Math.max(60000, last - first)
    const start = fixedWindow ? Number(windowStart) : first - (span - (last - first)) / 2
    const segments = [], points = []
    let run = [], previousAt = null, previousBucket = null
    function finish() {
        if (run.length) segments.push({points: run, curves: smooth(run)})
        run = []
    }
    for (let i = 0; i < samples.length; ++i) {
        const sample = samples[i], at = Number(sample.at)
        const ok = sample.ok && Number(sample.latencyMs) >= 0
        const point = {x: left + (at - start) / span * width, y: y(ok ? Number(sample.latencyMs) : 0),
                       from: left + ((sample.from === undefined ? at : Number(sample.from)) - start) / span * width,
                       to: left + ((sample.to === undefined ? at : Number(sample.to)) - start) / span * width,
                       high: y(ok ? Number(sample.maximum >= 0 ? sample.maximum : sample.latencyMs) : 0),
                       low: y(ok ? Number(sample.minimum >= 0 ? sample.minimum : sample.latencyMs) : 0),
                       failureFraction: sample.count > 0 ? Number(sample.failures || 0) / sample.count : ok ? 0 : 1,
                       index: i, ok: ok}
        points.push(point)
        const bucketGap = sample.bucket !== undefined && previousBucket !== null && sample.bucket > previousBucket + 1
        if (previousAt !== null && (at <= previousAt || bucketGap || at - previousAt > gap)) finish()
        if (ok) run.push(point)
        else finish()
        previousAt = at
        previousBucket = sample.bucket === undefined ? null : sample.bucket
    }
    finish()
    return {segments: segments, points: points, start: start, span: span, ceiling: ceiling,
            logarithmic: logarithmic, ticks: ticks, hasResponses: successful.length > 0}
}
