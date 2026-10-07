/**
 * RPS Point Set Library — persists named point sets in localStorage.
 *
 * Each set is a named collection of RPS points (coordinates + lock config).
 * Users define a set once, then select it by name for future inspections.
 */

export interface StoredRPSPoint {
  x: number; y: number; z: number
  lockX: boolean; lockY: boolean; lockZ: boolean; lockNormal: boolean
  weight: number
}

export interface RPSPointSet {
  id: string
  name: string
  points: StoredRPSPoint[]
  createdAt: string
  updatedAt: string
}

const STORAGE_KEY = 'alignmesh-rps-point-sets'

const BUILTIN_SETS: RPSPointSet[] = [
  {
    id: 'builtin-kawa6',
    name: 'kawa6',
    points: [
      { x: 69.135, y:   2.812, z: 471.729, lockX: false, lockY: false, lockZ: true, lockNormal: false, weight: 3 },
      { x: 80.462, y:  -8.957, z: 470.314, lockX: false, lockY: false, lockZ: true, lockNormal: false, weight: 3 },
      { x: 68.013, y:  -7.064, z: 383.998, lockX: false, lockY: false, lockZ: true, lockNormal: false, weight: 3 },
      { x: 77.554, y: -15.609, z: 466.472, lockX: false, lockY: false, lockZ: true, lockNormal: false, weight: 2 },
      { x: 59.361, y: -28.045, z: 373.816, lockX: false, lockY: false, lockZ: true, lockNormal: false, weight: 2 },
      { x: 72.208, y: -19.391, z: 439.108, lockX: false, lockY: false, lockZ: true, lockNormal: false, weight: 1 },
    ],
    createdAt: '2025-01-01T00:00:00.000Z',
    updatedAt: '2025-01-01T00:00:00.000Z',
  },
]

function generateId(): string {
  return Date.now().toString(36) + Math.random().toString(36).slice(2, 7)
}

export function loadAllSets(): RPSPointSet[] {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)
    const user: RPSPointSet[] = raw ? JSON.parse(raw) as RPSPointSet[] : []
    // Merge built-in sets (skip if user already has one with the same id)
    const userIds = new Set(user.map(s => s.id))
    const merged = [...BUILTIN_SETS.filter(b => !userIds.has(b.id)), ...user]
    return merged
  } catch {
    return [...BUILTIN_SETS]
  }
}

function saveAllSets(sets: RPSPointSet[]): void {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(sets))
}

export function saveSet(name: string, points: StoredRPSPoint[]): RPSPointSet {
  const sets = loadAllSets()
  const now = new Date().toISOString()
  const newSet: RPSPointSet = {
    id: generateId(),
    name,
    points,
    createdAt: now,
    updatedAt: now,
  }
  sets.push(newSet)
  saveAllSets(sets)
  return newSet
}

export function updateSet(id: string, name: string, points: StoredRPSPoint[]): void {
  const sets = loadAllSets()
  const idx = sets.findIndex(s => s.id === id)
  if (idx < 0) return
  sets[idx].name = name
  sets[idx].points = points
  sets[idx].updatedAt = new Date().toISOString()
  saveAllSets(sets)
}

export function deleteSet(id: string): void {
  const sets = loadAllSets().filter(s => s.id !== id)
  saveAllSets(sets)
}

export function renameSet(id: string, newName: string): void {
  const sets = loadAllSets()
  const s = sets.find(s => s.id === id)
  if (s) {
    s.name = newName
    s.updatedAt = new Date().toISOString()
    saveAllSets(sets)
  }
}
