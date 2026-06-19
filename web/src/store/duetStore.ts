import { create } from 'zustand'
import type { DuetModel, DuetHeater, DuetAxis, DuetFan, DuetTool, DuetJob } from '../services/duetApi'

export interface TempSample {
  time: number // seconds since start
  label: string // mm:ss
  heaters: number[] // current temp per heater index
}

interface DuetStore {
  // Full model snapshot
  model: DuetModel | null
  setModel: (m: DuetModel) => void

  // Temperature history (last 200 samples)
  tempHistory: TempSample[]
  pushTempSample: (heaters: DuetHeater[]) => void
  clearTempHistory: () => void

  // Console log
  consoleLines: string[]
  pushConsoleLine: (line: string) => void
  clearConsole: () => void

  // Command history for console (up/down arrows)
  commandHistory: string[]
  pushCommand: (cmd: string) => void

  // Polling state
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
  pushTempSample: (heaters) => {
    const elapsed = Math.floor((Date.now() - startTime) / 1000)
    const mins = Math.floor(elapsed / 60)
    const secs = elapsed % 60
    const label = `${mins}:${secs.toString().padStart(2, '0')}`
    const sample: TempSample = {
      time: elapsed,
      label,
      heaters: heaters.map(h => h.current),
    }
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
  return useDuetStore(s => s.model?.heat?.heaters ?? [])
}

export function useAxes(): DuetAxis[] {
  return useDuetStore(s => s.model?.move?.axes ?? [])
}

export function useFans(): DuetFan[] {
  return useDuetStore(s => s.model?.fans ?? [])
}

export function useTools(): DuetTool[] {
  return useDuetStore(s => s.model?.tools ?? [])
}

export function useJob(): DuetJob | null {
  return useDuetStore(s => s.model?.job ?? null)
}

export function useMachineStatus(): string {
  return useDuetStore(s => s.model?.state?.status ?? 'idle')
}
