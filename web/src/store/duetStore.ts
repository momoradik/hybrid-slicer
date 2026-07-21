import { create } from 'zustand'
import type { DuetModel, DuetHeater, DuetAxis, DuetFan, DuetTool, DuetJob } from '../services/duetApi'

export interface TempSample {
  time: number // seconds since start
  label: string // mm:ss
  heaters: Record<string, number> // keyed by heater label, not index
}

/** Build a display label for a heater given the model context */
export function heaterLabel(index: number, model: DuetModel | null): string {
  if (!model) return `Heater ${index}`
  const bedHeaters = model.heat?.bedHeaters ?? []
  const chamberHeaters = model.heat?.chamberHeaters ?? []
  if (bedHeaters.includes(index)) return 'Bed'
  if (chamberHeaters.includes(index)) return 'Chamber'
  // Check if a tool uses this heater
  const tool = model.tools?.find(t => t?.heaters?.includes(index))
  if (tool) {
    const hi = tool.heaters.indexOf(index)
    const toolName = tool.name || `T${tool.number}`
    return tool.heaters.length > 1 ? `${toolName} H${hi}` : toolName
  }
  return `Heater ${index}`
}

/** Get only valid (non-null) heaters with their original indices */
export function validHeaters(model: DuetModel | null): { heater: DuetHeater; index: number; label: string }[] {
  if (!model?.heat?.heaters) return []
  return model.heat.heaters
    .map((h, i) => ({ heater: h, index: i, label: heaterLabel(i, model) }))
    .filter(h => h.heater != null && h.heater.current !== undefined)
}

interface DuetStore {
  model: DuetModel | null
  setModel: (m: DuetModel) => void

  tempHistory: TempSample[]
  pushTempSample: (model: DuetModel) => void
  clearTempHistory: () => void

  consoleLines: string[]
  pushConsoleLine: (line: string) => void
  clearConsole: () => void

  commandHistory: string[]
  pushCommand: (cmd: string) => void

  polling: boolean
  setPolling: (v: boolean) => void
}

const MAX_TEMP_SAMPLES = 200
const MAX_CONSOLE_LINES = 500
const MAX_COMMAND_HISTORY = 100

let startTime = Date.now()

export const useDuetStore = create<DuetStore>((set) => ({
  model: null,
  setModel: (m) => set({ model: m }),

  tempHistory: [],
  pushTempSample: (model) => {
    const elapsed = Math.floor((Date.now() - startTime) / 1000)
    const mins = Math.floor(elapsed / 60)
    const secs = elapsed % 60
    const label = `${mins}:${secs.toString().padStart(2, '0')}`

    const entries = validHeaters(model)
    const heaters: Record<string, number> = {}
    for (const e of entries) {
      heaters[e.label] = Math.round(e.heater.current * 10) / 10
    }

    const sample: TempSample = { time: elapsed, label, heaters }
    set(state => ({
      tempHistory: [...state.tempHistory.slice(-(MAX_TEMP_SAMPLES - 1)), sample],
    }))
  },
  clearTempHistory: () => {
    startTime = Date.now()
    set({ tempHistory: [] })
  },

  consoleLines: [],
  pushConsoleLine: (line) => set(state => ({
    consoleLines: [...state.consoleLines.slice(-(MAX_CONSOLE_LINES - 1)), line],
  })),
  clearConsole: () => set({ consoleLines: [] }),

  commandHistory: [],
  pushCommand: (cmd) => set(state => ({
    commandHistory: [...state.commandHistory.slice(-(MAX_COMMAND_HISTORY - 1)), cmd],
  })),

  polling: false,
  setPolling: (v) => set({ polling: v }),
}))

// ── Convenience selectors ────────────────────────────────────────────────

export function useHeaters(): DuetHeater[] {
  return useDuetStore(s => {
    const heaters = s.model?.heat?.heaters ?? []
    return heaters.filter(h => h != null)
  })
}

export function useAxes(): DuetAxis[] {
  return useDuetStore(s => s.model?.move?.axes ?? [])
}

export function useFans(): DuetFan[] {
  return useDuetStore(s => (s.model?.fans ?? []).filter(f => f != null))
}

export function useTools(): DuetTool[] {
  return useDuetStore(s => (s.model?.tools ?? []).filter(t => t != null))
}

export function useJob(): DuetJob | null {
  return useDuetStore(s => s.model?.job ?? null)
}

export function useMachineStatus(): string {
  return useDuetStore(s => s.model?.state?.status ?? 'idle')
}
