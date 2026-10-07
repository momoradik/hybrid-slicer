/**
 * Metrology-standard deviation colormap.
 *
 * FIREWALL: maps CORE-supplied signed deviation values to colors.
 * This is pure display — the deviation values are never computed here.
 *
 * Professional diverging colormap centered at 0:
 *   Blue (negative, under/less material) → Green (in-tolerance) → Red (positive, over/more material)
 *   Symmetric about 0. Perceptually smooth. Clear tolerance boundary.
 *   Gray: no-data / no-correspondence (NEVER green)
 *   Saturated magenta: out-of-range (|deviation| > display range)
 */

export interface RGB { r: number; g: number; b: number }

export type ColorMode = 'per-vertex' | 'per-facet'
export type DisplayMode = 'deviation' | 'go-nogo'

/** No-data color: gray (NEVER green). */
export const COLOR_NO_DATA: RGB = { r: 0.5, g: 0.5, b: 0.5 }

/** Out-of-range color: saturated magenta. */
export const COLOR_OUT_OF_RANGE: RGB = { r: 1.0, g: 0.0, b: 1.0 }

/** Max-deviation marker color: bright white. */
export const COLOR_MAX_MARKER: RGB = { r: 1.0, g: 1.0, b: 1.0 }

// ── Smooth diverging ramp (blue → green → red) ─────────────────────
// Uses cubic Hermite interpolation for perceptual smoothness.
// The ramp is parameterized as t in [-1, +1]:
//   -1 = most negative (blue)
//    0 = zero deviation (green)
//   +1 = most positive (red)

function smoothRamp(t: number): RGB {
  // t in [-1, 1]. Clamp for safety.
  t = Math.max(-1, Math.min(1, t))

  if (t < 0) {
    // Blue → Green: negative side
    const s = -t // 0 at center, 1 at max negative
    // Smooth cubic interpolation
    const s2 = s * s
    const s3 = s2 * s
    return {
      r: 0,
      g: 1.0 - s2 * 0.6 - s3 * 0.4,   // green fades from 1.0 to 0
      b: s2 * 0.4 + s * 0.6,            // blue rises from 0 to 1.0
    }
  } else {
    // Green → Red: positive side
    const s = t // 0 at center, 1 at max positive
    const s2 = s * s
    const s3 = s2 * s
    return {
      r: s2 * 0.4 + s * 0.6,            // red rises from 0 to 1.0
      g: 1.0 - s2 * 0.6 - s3 * 0.4,   // green fades from 1.0 to 0
      b: 0,
    }
  }
}

/**
 * Map a signed deviation to a color using the metrology colormap.
 *
 * @param deviation  Signed deviation value (mm) from the core.
 * @param tolerance  Tolerance band half-width (mm).
 * @param rangeMax   Display range maximum (mm). Values beyond are out-of-range.
 * @returns          RGB color in [0,1].
 */
export function deviationToMetrologyColor(
  deviation: number,
  tolerance: number,
  rangeMax: number,
): RGB {
  if (deviation === undefined || deviation === null || isNaN(deviation)) {
    return { ...COLOR_NO_DATA }
  }

  const absDev = Math.abs(deviation)

  if (absDev > rangeMax) {
    return { ...COLOR_OUT_OF_RANGE }
  }

  // Two-zone mapping:
  // Within tolerance: green nominal band (slight shading by proximity to edge)
  // Outside tolerance: remap [tolerance, rangeMax] to full warm/cool ramp

  if (absDev <= tolerance) {
    // GREEN nominal band. Shade slightly by proximity to the edge.
    const t = absDev / Math.max(tolerance, 1e-20) // 0 at center, 1 at edge
    return { r: t * 0.25, g: 0.7 + (1 - t) * 0.3, b: t * 0.15 }
  }

  // Outside tolerance: map the excess to a 0→1 ramp.
  const overTol = (absDev - tolerance) / Math.max(rangeMax - tolerance, 1e-20)
  const s = Math.min(1, Math.max(0, overTol))

  if (deviation > 0) {
    // Positive (excess material): orange → red
    return smoothRamp(0.65 + s * 0.35)
  } else {
    // Negative (deficit): teal → blue
    return smoothRamp(-(0.65 + s * 0.35))
  }
}

/** Go/No-Go binary: green if within tolerance, red if not. */
export function goNoGoColor(deviation: number, tolerance: number): RGB {
  if (deviation === undefined || deviation === null || isNaN(deviation)) {
    return { ...COLOR_NO_DATA }
  }
  return Math.abs(deviation) <= tolerance
    ? { r: 0, g: 0.8, b: 0 }   // GO: green
    : { r: 0.9, g: 0, b: 0 }   // NO-GO: red
}

/** Classification of a single deviation point. */
export type PointClass = 'in-tolerance' | 'out-positive' | 'out-negative' | 'no-data' | 'out-of-range'

export function classifyDeviation(
  deviation: number, tolerance: number, rangeMax: number,
): PointClass {
  if (deviation === undefined || deviation === null || isNaN(deviation)) return 'no-data'
  if (Math.abs(deviation) > rangeMax) return 'out-of-range'
  if (Math.abs(deviation) <= tolerance) return 'in-tolerance'
  return deviation > 0 ? 'out-positive' : 'out-negative'
}

/**
 * Process a full deviation field from the core into display data.
 *
 * FIREWALL: reads core values only. The per-point deviations are the
 * core's values; the colors are display transforms.
 */
export interface DeviationDisplayData {
  colors: RGB[]
  classes: PointClass[]
  maxDeviationIndex: number
  maxDeviationValue: number
  inToleranceCount: number
  outOfToleranceCount: number
  noDataCount: number
  outOfRangeCount: number
  percentInTolerance: number
}

export function processDeviationField(
  deviations: number[],
  tolerance: number,
  rangeMax: number,
  mode: DisplayMode = 'deviation',
): DeviationDisplayData {
  const colors: RGB[] = []
  const classes: PointClass[] = []
  let maxIdx = 0
  let maxVal = 0
  let inTol = 0, outTol = 0, noData = 0, oor = 0

  for (let i = 0; i < deviations.length; i++) {
    const d = deviations[i]
    const cls = classifyDeviation(d, tolerance, rangeMax)
    classes.push(cls)

    if (mode === 'go-nogo') {
      colors.push(cls === 'no-data' ? { ...COLOR_NO_DATA } : goNoGoColor(d, tolerance))
    } else {
      colors.push(deviationToMetrologyColor(d, tolerance, rangeMax))
    }

    if (cls === 'in-tolerance') inTol++
    else if (cls === 'no-data') noData++
    else if (cls === 'out-of-range') { oor++; outTol++ }
    else outTol++

    if (d !== undefined && !isNaN(d) && Math.abs(d) > Math.abs(maxVal)) {
      maxVal = d
      maxIdx = i
    }
  }

  const total = deviations.length - noData
  return {
    colors,
    classes,
    maxDeviationIndex: maxIdx,
    maxDeviationValue: maxVal,
    inToleranceCount: inTol,
    outOfToleranceCount: outTol,
    noDataCount: noData,
    outOfRangeCount: oor,
    percentInTolerance: total > 0 ? (inTol / total) * 100 : 0,
  }
}
