import axios from 'axios'
import type {
  MachineProfile, PrintProfile, CncTool,
  PrintJob, CustomGCodeBlock, BrandingSettings, Material
} from '../types'

const http = axios.create({ baseURL: '/api' })

// ── Machine Profiles ──────────────────────────────────────────────────────
export const machineProfilesApi = {
  getAll: () => http.get<MachineProfile[]>('/machine-profiles').then(r => r.data),
  getById: (id: string) => http.get<MachineProfile>(`/machine-profiles/${id}`).then(r => r.data),
  create: (data: Partial<MachineProfile>) => http.post<MachineProfile>('/machine-profiles', data).then(r => r.data),
  update: (id: string, data: Partial<MachineProfile>) => http.put<MachineProfile>(`/machine-profiles/${id}`, data).then(r => r.data),
  updateOffsets: (id: string, offsets: object) => http.put(`/machine-profiles/${id}/offsets`, offsets).then(r => r.data),
  delete: (id: string) => http.delete(`/machine-profiles/${id}`),
}

// ── Print Profiles ────────────────────────────────────────────────────────
export const printProfilesApi = {
  getAll: () => http.get<PrintProfile[]>('/print-profiles').then(r => r.data),
  getById: (id: string) => http.get<PrintProfile>(`/print-profiles/${id}`).then(r => r.data),
  create: (data: Partial<PrintProfile>) => http.post<PrintProfile>('/print-profiles', data).then(r => r.data),
  update: (id: string, data: Partial<PrintProfile>) => http.put<PrintProfile>(`/print-profiles/${id}`, data).then(r => r.data),
  delete: (id: string) => http.delete(`/print-profiles/${id}`),
}

// ── CNC Tools ─────────────────────────────────────────────────────────────
export const toolsApi = {
  getAll: () => http.get<CncTool[]>('/tools').then(r => r.data),
  getById: (id: string) => http.get<CncTool>(`/tools/${id}`).then(r => r.data),
  create: (data: Partial<CncTool>) => http.post<CncTool>('/tools', data).then(r => r.data),
  update: (id: string, data: object) => http.put<CncTool>(`/tools/${id}`, data).then(r => r.data),
  delete: (id: string) => http.delete(`/tools/${id}`),
}

// ── Jobs ──────────────────────────────────────────────────────────────────
export const jobsApi = {
  getAll: () => http.get<PrintJob[]>('/jobs').then(r => r.data),
  getById: (id: string) => http.get<PrintJob>(`/jobs/${id}`).then(r => r.data),

  uploadStl: (formData: FormData) =>
    http.post<{ jobId: string }>('/jobs/upload-stl', formData, {
      headers: { 'Content-Type': 'multipart/form-data' },
    }).then(r => r.data),

  slice: (id: string) => http.post(`/jobs/${id}/slice`).then(r => r.data),

  generateToolpaths: (
    id: string, toolId: string, machineEveryN: number,
    machineInnerWalls = false, avoidSupports = false,
    supportClearanceMm = 2.0,
    autoMachiningFrequency = false,
    zSafetyOffsetMm = 0,
    spindleRpmOverride: number | null = null,
    spindleStartX = 0, spindleStartY = 0, spindleStartZ: number | null = null,
    spindleEndX = 0, spindleEndY = 0, spindleEndZ: number | null = null,
    skipMachiningLayers = 0,
  ) =>
    http.post(`/jobs/${id}/generate-toolpaths`, {
      cncToolId: toolId,
      machineEveryNLayers: machineEveryN,
      machineInnerWalls,
      avoidSupports,
      supportClearanceMm,
      autoMachiningFrequency,
      zSafetyOffsetMm,
      spindleRpmOverride,
      spindleStartX, spindleStartY, spindleStartZ,
      spindleEndX, spindleEndY, spindleEndZ,
      skipMachiningLayers,
    }).then(r => r.data),

  planHybrid: (id: string, machineEveryN: number) =>
    http.post(`/jobs/${id}/plan-hybrid`, { machineEveryNLayers: machineEveryN }).then(r => r.data),

  getPrintGCode: (id: string) =>
    http.get<string>(`/jobs/${id}/print-gcode`, { responseType: 'text' }).then(r => r.data),

  getToolpathGCode: (id: string) =>
    http.get<string>(`/jobs/${id}/toolpath-gcode`, { responseType: 'text' }).then(r => r.data),

  downloadGCode: (id: string) =>
    http.get(`/jobs/${id}/gcode`, { responseType: 'blob' }).then(r => r.data),

  getHybridGCode: (id: string) =>
    http.get<string>(`/jobs/${id}/gcode`, { responseType: 'text' }).then(r => r.data),

  mergeBeds: (
    jobIds: string[], layerStep: number, name?: string,
    hybrid = false, cncParams?: {
      cncToolId: string; machineEveryNLayers: number;
      machineInnerWalls: boolean; avoidSupports: boolean;
      supportClearanceMm: number; autoMachiningFrequency: boolean;
      zSafetyOffsetMm: number; spindleRpmOverride: number | null;
    },
  ) =>
    http.post<{ jobId: string; mergedPath: string; beds: number; layerStep: number; totalLayers: number }>(
      '/jobs/merge-beds', { jobIds, layerStep, name, hybrid, ...cncParams }).then(r => r.data),

  deleteJob: (id: string) => http.delete(`/jobs/${id}`),
}

// ── Machine connection (printer link) ─────────────────────────────────────
export type MachineTransport = 'Network' | 'Serial'

export interface MachineConnectionStatus {
  isConnected: boolean
  transport: MachineTransport | null
  /** "192.168.1.20" for network, "COM3 @ 115200" for serial. */
  endpoint: string | null
  firmwareDescription: string | null
  /** False over USB — RepRapFirmware serves file endpoints over HTTP only. */
  supportsFileTransfer: boolean
}

export interface FirmwareOption {
  id: string
  name: string
  supported: boolean
  note: string
}

export interface ConnectRequest {
  transport: MachineTransport
  firmware: string
  host?: string
  port?: number
  password?: string
  portName?: string
  baudRate?: number
}

export const machineConnectionApi = {
  getStatus: () => http.get<MachineConnectionStatus>('/machine/connection').then(r => r.data),
  getSerialPorts: () => http.get<{ ports: string[] }>('/machine/serial-ports').then(r => r.data.ports),
  getFirmwares: () => http.get<{ firmwares: FirmwareOption[] }>('/machine/firmwares').then(r => r.data.firmwares),
  connect: (req: ConnectRequest) =>
    http.post<MachineConnectionStatus>('/machine/connect', req).then(r => r.data),
  disconnect: () => http.post<MachineConnectionStatus>('/machine/disconnect').then(r => r.data),
}

// ── Custom G-code Blocks ──────────────────────────────────────────────────
export const customGCodeApi = {
  /** Omit machineProfileId for every block; pass one for that machine's blocks + the shared ones. */
  getAll: (machineProfileId?: string) =>
    http.get<CustomGCodeBlock[]>('/custom-gcode-blocks', {
      params: machineProfileId ? { machineProfileId } : undefined,
    }).then(r => r.data),
  create: (data: Partial<CustomGCodeBlock>) => http.post<CustomGCodeBlock>('/custom-gcode-blocks', data).then(r => r.data),
  update: (id: string, data: Partial<CustomGCodeBlock>) => http.put<CustomGCodeBlock>(`/custom-gcode-blocks/${id}`, data).then(r => r.data),
  toggle: (id: string, enabled: boolean) => http.patch(`/custom-gcode-blocks/${id}/toggle`, { enabled }),
  delete: (id: string) => http.delete(`/custom-gcode-blocks/${id}`),
}

// ── Branding ──────────────────────────────────────────────────────────────
export const brandingApi = {
  get: () => http.get<BrandingSettings>('/branding').then(r => r.data),
  update: (data: BrandingSettings) => http.put<BrandingSettings>('/branding', data).then(r => r.data),
}

// ── Materials ─────────────────────────────────────────────────────────────
export const materialsApi = {
  getAll: () => http.get<Material[]>('/materials').then(r => r.data),
  create: (data: Partial<Material>) => http.post<Material>('/materials', data).then(r => r.data),
  update: (id: string, data: Partial<Material>) => http.put<Material>(`/materials/${id}`, data).then(r => r.data),
  delete: (id: string) => http.delete(`/materials/${id}`),
}
