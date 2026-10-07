/**
 * Format detection from file extension.
 * Extracted for testability. No computation — just string matching.
 */

export interface DetectedFormat {
  format: string
  label: string
  isPointCloud: boolean
  isCAD: boolean
}

const FORMAT_FROM_EXT: Record<string, DetectedFormat> = {
  '.stl':  { format: 'stl',  label: 'STL (tessellated, float32)',    isPointCloud: false, isCAD: false },
  '.ply':  { format: 'ply',  label: 'PLY (mesh/cloud)',              isPointCloud: false, isCAD: false },
  '.obj':  { format: 'obj',  label: 'OBJ (mesh)',                    isPointCloud: false, isCAD: false },
  '.step': { format: 'step', label: 'STEP/CAD (analytic BREP)',      isPointCloud: false, isCAD: true },
  '.stp':  { format: 'step', label: 'STEP/CAD (analytic BREP)',      isPointCloud: false, isCAD: true },
  '.e57':  { format: 'e57',  label: 'E57 (point cloud)',             isPointCloud: true,  isCAD: false },
  '.xyz':  { format: 'xyz',  label: 'XYZ (point cloud, ASCII)',      isPointCloud: true,  isCAD: false },
  '.pts':  { format: 'pts',  label: 'PTS (point cloud)',             isPointCloud: true,  isCAD: false },
  '.asc':  { format: 'asc',  label: 'ASC (point cloud)',             isPointCloud: true,  isCAD: false },
}

export function detectFormat(path: string): DetectedFormat | null {
  const ext = '.' + (path.split('.').pop()?.toLowerCase() ?? '')
  return FORMAT_FROM_EXT[ext] ?? null
}

/** Check if a format is accepted for a given role. */
export function isAcceptedFormat(format: string, role: 'reference' | 'measured'): boolean {
  const refFormats = ['stl', 'ply', 'obj', 'step']
  const measFormats = ['stl', 'ply', 'obj', 'e57', 'xyz', 'pts', 'asc']
  return role === 'reference' ? refFormats.includes(format) : measFormats.includes(format)
}
