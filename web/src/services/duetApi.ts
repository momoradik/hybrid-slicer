import axios from 'axios'

const http = axios.create({ baseURL: '/api/duet' })

// ── Types for Duet Object Model ──────────────────────────────────────────

export interface DuetHeater {
  current: number
  active: number
  standby: number
  state: string // 'off' | 'standby' | 'active' | 'fault' | 'tuning'
  sensor: number
}

export interface DuetTool {
  number: number
  name: string
  heaters: number[]
  extruders: number[]
  fans: number[]
  active: number[]
  standby: number[]
  state: string
}

export interface DuetFan {
  actualValue: number
  requestedValue: number
  rpm: number
  name: string
}

export interface DuetAxis {
  letter: string
  machinePosition: number
  userPosition: number
  min: number
  max: number
  homed: boolean
  visible: boolean
}

export interface DuetExtruder {
  position: number
  factor: number
}

export interface DuetJob {
  file: {
    fileName: string | null
    size: number
    height: number
    layerHeight: number
    filament: number[]
    generatedBy: string
    printTime: number | null
    firstLayerHeight: number
  } | null
  filePosition: number | null
  lastFileName: string | null
  duration: number | null
  warmUpDuration: number | null
  layer: number | null
  timesLeft: {
    file: number | null
    filament: number | null
    slicer: number | null
  } | null
}

export interface DuetBedOrChamber {
  active: number
  standby: number
  heaters: number[]
}

export interface DuetMove {
  axes: DuetAxis[]
  extruders: DuetExtruder[]
  speedFactor: number
  currentMove?: {
    requestedSpeed: number
    topSpeed: number
  }
  babystepZ: number
}

export interface DuetHeat {
  heaters: DuetHeater[]
  bedHeaters: number[]
  chamberHeaters: number[]
}

export interface DuetState {
  status: string // 'I'dle, 'P'rinting, 'S'topped, 'C'onfiguring, 'D'ecelerating, 'B'usy, etc.
  machineMode: string
  displayMessage: string | null
  atxPower: boolean | null
  currentTool: number
  upTime: number
}

export interface DuetNetwork {
  name: string
  interfaces: Array<{
    type: string
    actualIP: string
    firmwareVersion: string
  }>
}

export interface DuetBoards {
  firmwareName: string
  firmwareVersion: string
  shortName: string
  name: string
}

export interface DuetSensor {
  name: string | null
  type: string
  lastReading: number | null
}

export interface DuetModel {
  state: DuetState
  move: DuetMove
  heat: DuetHeat
  fans: DuetFan[]
  tools: DuetTool[]
  job: DuetJob
  network?: DuetNetwork
  boards?: DuetBoards[]
  sensors?: {
    analog: DuetSensor[]
  }
  volumes?: Array<{
    mounted: boolean
    totalSpace: number
    freeSpace: number
    path: string | null
  }>
}

export interface DuetFileInfo {
  type: 'd' | 'f'
  name: string
  size: number
  date: string
}

export interface DuetFileList {
  dir: string
  files: DuetFileInfo[]
  next: number // 0 = no more pages
  err?: number
}

// ── Status display helpers ───────────────────────────────────────────────

/** Convert RRF 3.x status string to a display label.
 *  RRF 3.x rr_model returns full words: "idle", "processing", "simulating", etc.
 *  RRF 2.x rr_status returns single chars: "I", "P", "S", etc.
 *  We handle both. */
export function statusLabel(s: string): string {
  // RRF 3.x full strings
  const map3: Record<string, string> = {
    idle: 'Idle', busy: 'Busy', processing: 'Printing', paused: 'Paused',
    resuming: 'Resuming', halted: 'Halted', flashing: 'Flashing Firmware',
    changingTool: 'Changing Tool', simulating: 'Simulating', off: 'Off',
    configuring: 'Configuring', updating: 'Updating',
  }
  // RRF 2.x single-char codes
  const map2: Record<string, string> = {
    I: 'Idle', P: 'Printing', S: 'Stopped', C: 'Configuring',
    D: 'Decelerating', R: 'Resuming', H: 'Halted',
    F: 'Flashing Firmware', T: 'Changing Tool', B: 'Busy',
    M: 'Simulating', O: 'Off',
  }
  return map3[s] ?? map2[s] ?? s.charAt(0).toUpperCase() + s.slice(1)
}

/** Check if status means "currently printing" (works for both RRF 2.x and 3.x) */
export function isPrinting(s: string): boolean {
  return s === 'P' || s === 'processing' || s === 'resuming' || s === 'R'
}

/** Check if status means "paused" (works for both RRF 2.x and 3.x) */
export function isPaused(s: string): boolean {
  return s === 'S' || s === 'paused' || s === 'D'
}

/** Status color class for badge display */
export function statusColor(s: string): string {
  if (isPrinting(s)) return 'bg-blue-900 text-blue-300 animate-pulse'
  if (isPaused(s)) return 'bg-yellow-900 text-yellow-300'
  const colors: Record<string, string> = {
    idle: 'bg-green-900 text-green-300', I: 'bg-green-900 text-green-300',
    halted: 'bg-red-900 text-red-300', H: 'bg-red-900 text-red-300',
    off: 'bg-gray-700 text-gray-400', O: 'bg-gray-700 text-gray-400',
    busy: 'bg-yellow-900 text-yellow-300', B: 'bg-yellow-900 text-yellow-300',
    simulating: 'bg-purple-900 text-purple-300', M: 'bg-purple-900 text-purple-300',
    configuring: 'bg-yellow-900 text-yellow-300', C: 'bg-yellow-900 text-yellow-300',
  }
  return colors[s] ?? 'bg-gray-700 text-gray-300'
}

// ── API Calls ────────────────────────────────────────────────────────────

/** Get the full or partial object model */
export async function getModel(key = '', flags = 'd99vn'): Promise<DuetModel> {
  const params = new URLSearchParams()
  if (key) params.set('key', key)
  params.set('flags', flags)
  const { data } = await http.get<{ result: DuetModel }>(`/rr_model?${params}`)
  return data.result ?? data as unknown as DuetModel
}

/** Get status (legacy rr_status type 1/2/3) */
export async function getStatus(type = 2): Promise<Record<string, unknown>> {
  const { data } = await http.get(`/rr_status?type=${type}`)
  return data
}

/** Send G-code command */
export async function sendGCode(code: string): Promise<void> {
  await http.get(`/rr_gcode?gcode=${encodeURIComponent(code)}`)
}

/** Get reply from last command */
export async function getReply(): Promise<string> {
  const { data } = await http.get('/rr_reply', { responseType: 'text' })
  return typeof data === 'string' ? data : JSON.stringify(data)
}

/** Send G-code and get reply */
export async function sendGCodeWithReply(code: string): Promise<string> {
  await sendGCode(code)
  await new Promise(r => setTimeout(r, 100)) // brief delay for firmware
  return getReply()
}

/** List files in a directory */
export async function listFiles(dir: string, first = 0): Promise<DuetFileList> {
  const { data } = await http.get<DuetFileList>(`/rr_filelist?dir=${encodeURIComponent(dir)}&first=${first}`)
  return data
}

/** Get file info for a G-code file */
export async function getFileInfo(name?: string): Promise<Record<string, unknown>> {
  const qs = name ? `?name=${encodeURIComponent(name)}` : ''
  const { data } = await http.get(`/rr_fileinfo${qs}`)
  return data
}

/** Download a file from the Duet SD card */
export async function downloadFile(name: string): Promise<Blob> {
  const { data } = await http.get(`/rr_download?name=${encodeURIComponent(name)}`, { responseType: 'blob' })
  return data
}

/** Upload a file to the Duet SD card */
export async function uploadFile(name: string, content: ArrayBuffer | Blob): Promise<void> {
  await http.post(`/rr_upload?name=${encodeURIComponent(name)}`, content, {
    headers: { 'Content-Type': 'application/octet-stream' },
  })
}

/** Delete a file on the SD card */
export async function deleteFile(name: string): Promise<void> {
  await http.get(`/rr_delete?name=${encodeURIComponent(name)}`)
}

/** Create a directory on the SD card */
export async function makeDirectory(dir: string): Promise<void> {
  await http.get(`/rr_mkdir?dir=${encodeURIComponent(dir)}`)
}

/** Move/rename a file */
export async function moveFile(oldPath: string, newPath: string): Promise<void> {
  await http.get(`/rr_move?old=${encodeURIComponent(oldPath)}&new=${encodeURIComponent(newPath)}`)
}

/** Download heightmap.csv */
export async function getHeightMap(): Promise<string> {
  const blob = await downloadFile('0:/sys/heightmap.csv')
  return blob.text()
}
