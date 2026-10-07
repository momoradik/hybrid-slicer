/**
 * Landmark/RPS point definition logic.
 *
 * FIREWALL NOTE: this module performs coordinate-frame CONVERSION
 * (input preparation), not measurement computation. The conversion
 * from reference-datum-relative to file-frame is exact math that
 * prepares the input for the core. The core computes the fit.
 *
 * The UI solves nothing — it sends transformed points to the core
 * and displays the core's returned fit/residuals.
 */

/** A single landmark/RPS point. */
export interface LandmarkPoint {
  id: string
  /** Coordinates as entered by the user (in the active frame). */
  entered: [number, number, number]
  /** Coordinates in the file/analytical frame (sent to the core). */
  fileFrame: [number, number, number]
  /** Per-axis locks: which directions this point constrains. */
  locks: { x: boolean; y: boolean; z: boolean }
  /** Weight (default 1.0). */
  weight: number
  /** Label (e.g., "P1", "RPS-A"). */
  label: string
}

/** A reference datum/frame for Mode B. */
export interface ReferenceDatum {
  /** Translation from file origin. */
  translation: [number, number, number]
  /** Rotation about Z axis (degrees). */
  rotationZ: number
}

/** Convert a point from reference-datum-relative to file-frame coordinates.
 *
 * file = R(rotZ) * relative + translation
 *
 * where R(rotZ) is the 2D rotation matrix in the XY plane:
 *   [ cos(θ)  -sin(θ)  0 ]   [ x ]   [ tx ]
 *   [ sin(θ)   cos(θ)  0 ] * [ y ] + [ ty ]
 *   [   0        0     1 ]   [ z ]   [ tz ]
 *
 * This is EXACT — no approximation, no truncation.
 */
export function datumToFileFrame(
  relative: [number, number, number],
  datum: ReferenceDatum,
): [number, number, number] {
  const theta = datum.rotationZ * Math.PI / 180
  const c = Math.cos(theta)
  const s = Math.sin(theta)
  const [rx, ry, rz] = relative
  const [tx, ty, tz] = datum.translation

  return [
    c * rx - s * ry + tx,
    s * rx + c * ry + ty,
    rz + tz,
  ]
}

/** Identity datum (no offset, no rotation). */
export const IDENTITY_DATUM: ReferenceDatum = {
  translation: [0, 0, 0],
  rotationZ: 0,
}

/** Count how many DOFs are constrained by the current point set.
 *
 * A rigid alignment has exactly 6 DOFs — never more. Locked axes beyond
 * 6 are redundant/over-constraints, not additional DOFs.
 *
 * Full DOF analysis is done by the core — this is a UI guide only.
 */
export function countConstrainedDOFs(points: LandmarkPoint[]): {
  /** DOFs constrained, capped at 6 (rigid body maximum). */
  total: number
  lockedX: number
  lockedY: number
  lockedZ: number
  /** Total locked axes (may exceed 6 = redundant constraints). */
  totalLockedAxes: number
  /** Locked axes beyond 6 — redundant, not additional DOFs. */
  redundantConstraints: number
  isComplete: boolean
  warning: string | null
} {
  let lx = 0, ly = 0, lz = 0
  for (const p of points) {
    if (p.locks.x) lx++
    if (p.locks.y) ly++
    if (p.locks.z) lz++
  }

  const totalLockedAxes = lx + ly + lz
  // A rigid body has exactly 6 DOFs — cap at 6.
  const total = Math.min(totalLockedAxes, 6)
  const redundantConstraints = Math.max(0, totalLockedAxes - 6)
  const isComplete = lx >= 1 && ly >= 1 && lz >= 1 && totalLockedAxes >= 6

  let warning: string | null = null
  if (totalLockedAxes < 6) {
    warning = 'Under-constrained: ' + totalLockedAxes + '/6 DOFs locked. Need ' + (6 - totalLockedAxes) + ' more.'
  } else if (lx === 0) {
    warning = 'No X-axis lock — translation in X is unconstrained.'
  } else if (ly === 0) {
    warning = 'No Y-axis lock — translation in Y is unconstrained.'
  } else if (lz === 0) {
    warning = 'No Z-axis lock — translation in Z is unconstrained.'
  }

  return { total, lockedX: lx, lockedY: ly, lockedZ: lz, totalLockedAxes, redundantConstraints, isComplete, warning }
}

/** Build the payload to send to the core. */
export function buildCorePayload(points: LandmarkPoint[]) {
  return {
    landmarks: points.map(p => ({
      label: p.label,
      x: p.fileFrame[0],
      y: p.fileFrame[1],
      z: p.fileFrame[2],
      lock_x: p.locks.x,
      lock_y: p.locks.y,
      lock_z: p.locks.z,
      weight: p.weight,
    })),
  }
}
