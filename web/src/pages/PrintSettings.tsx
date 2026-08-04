import { useState, useCallback, useEffect, useRef } from 'react'
import { Link } from 'react-router-dom'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { printProfilesApi } from '../api/client'
import DisabledHint from '../components/DisabledHint'
import type { PrintProfile } from '../types'

// ── Cura-derived "Auto" defaults ────────────────────────────────────────────
// These replicate Cura's built-in formula defaults so the "Auto" button
// restores a real Cura-effective value, not a hardcoded guess.
//
//   layer_height    = nozzle * 0.5   (Cura default for standard nozzles)
//   line_width      = nozzle * 1.0
//   speed_print     = 60 mm/s        (Cura 5.x default)
//   speed_travel    = 150 mm/s
//   speed_wall_0    = speed_print * 0.5  (outer wall: half print speed)
//   speed_wall_x    = speed_print        (inner wall: same as print speed)
//   speed_infill    = speed_print * 1.5  (infill faster)
//   speed_layer_0   = 20 mm/s           (first layer slower)
//   material_flow   = 100 %

function curaDefaults(nozzle: number, printSpeed: number) {
  return {
    layerHeightMm:     +(nozzle * 0.5).toFixed(2),
    lineWidthMm:       +nozzle.toFixed(2),
    printSpeedMmS:     printSpeed,
    travelSpeedMmS:    150,
    wallSpeedMmS:      +(printSpeed * 0.5).toFixed(1),
    innerWallSpeedMmS: +printSpeed.toFixed(1),
    infillSpeedMmS:    +(printSpeed * 1.5).toFixed(1),
    firstLayerSpeedMmS: 20,
    materialFlowPct:   100,
    coolingFanSpeedPct: 100,
  }
}

type DraftProfile = Partial<PrintProfile> & { name: string }

const EMPTY: DraftProfile = {
  name: '',
  nozzleDiameterMm: 0.4,
  layerHeightMm: 0.2,
  lineWidthMm: 0.4,
  materialFlowPct: 100,
  printSpeedMmS: 60,
  travelSpeedMmS: 150,
  wallSpeedMmS: 30,
  innerWallSpeedMmS: 60,
  infillSpeedMmS: 90,
  firstLayerSpeedMmS: 20,
  wallCount: 3,
  infillDensityPct: 20,
  infillPattern: 'grid',
  printTemperatureDegC: 210,
  bedTemperatureDegC: 60,
  retractLengthMm: 5,
  coolingEnabled: true,
  coolingFanSpeedPct: 100,
  supportEnabled: false,
  pelletModeEnabled: false,
  virtualFilamentDiameterMm: 1.0,
}

export default function PrintSettings() {
  const qc = useQueryClient()
  const { data: profiles = [] } = useQuery({ queryKey: ['printProfiles'], queryFn: printProfilesApi.getAll })
  const [draft, setDraft]       = useState<DraftProfile | null>(null)
  const [advanced, setAdvanced] = useState(false)
  const [toast, setToast]       = useState<{ msg: string; ok: boolean } | null>(null)

  const showToast = (msg: string, ok = true) => {
    setToast({ msg, ok })
    setTimeout(() => setToast(null), 3000)
  }

  const createMutation = useMutation({
    mutationFn: (d: DraftProfile) => printProfilesApi.create(d),
    onSuccess: () => { qc.invalidateQueries({ queryKey: ['printProfiles'] }); setDraft(null); showToast('Profile created') },
    onError: (e: any) => showToast(e?.response?.data?.title ?? 'Failed to create profile', false),
  })
  const updateMutation = useMutation({
    mutationFn: (d: DraftProfile) => printProfilesApi.update(d.id!, d),
    onSuccess: () => { qc.invalidateQueries({ queryKey: ['printProfiles'] }); setDraft(null); showToast('Settings saved') },
    onError: (e: any) => showToast(e?.response?.data?.title ?? 'Failed to save settings', false),
  })
  const deleteMutation = useMutation({
    mutationFn: (id: string) => printProfilesApi.delete(id),
    onSuccess: () => qc.invalidateQueries({ queryKey: ['printProfiles'] }),
  })

  const set = useCallback(<K extends keyof DraftProfile>(key: K, val: DraftProfile[K]) =>
    setDraft(d => d ? { ...d, [key]: val } : d), [])

  const applyAuto = (field: keyof ReturnType<typeof curaDefaults>) => {
    if (!draft) return
    const nozzle = draft.nozzleDiameterMm ?? 0.4
    const printSpd = draft.printSpeedMmS ?? 60
    const defaults = curaDefaults(nozzle, printSpd)
    set(field as keyof DraftProfile, defaults[field] as any)
  }

  const applyAllAuto = () => {
    if (!draft) return
    const nozzle = draft.nozzleDiameterMm ?? 0.4
    const printSpd = draft.printSpeedMmS ?? 60
    const d = curaDefaults(nozzle, printSpd)
    setDraft(prev => prev ? { ...prev, ...d } : prev)
  }

  const save = () => {
    if (!draft) return
    if (draft.id) updateMutation.mutate(draft)
    else createMutation.mutate(draft)
  }

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <h2 className="text-2xl font-semibold text-white">Print Settings</h2>
        <button
          onClick={() => { setDraft({ ...EMPTY }); setAdvanced(false) }}
          className="px-4 py-2 bg-primary/80 hover:bg-primary text-white text-sm rounded-lg"
        >
          + New Profile
        </button>
      </div>

      {/* Profile list */}
      <div className="grid grid-cols-3 gap-4">
        {profiles.map(p => (
          <div key={p.id}
            className="bg-gray-900 border border-gray-800 hover:border-gray-600 rounded-xl p-4 transition space-y-2"
          >
            <div className="flex items-start justify-between gap-2">
              <p className="font-medium text-white truncate">{p.name}</p>
              <div className="flex gap-1 shrink-0">
                <button onClick={() => { setDraft({ ...EMPTY, ...p }); setAdvanced(false) }}
                  className="px-2 py-0.5 text-xs bg-gray-800 hover:bg-gray-700 text-gray-300 rounded">
                  Edit
                </button>
                <button onClick={() => deleteMutation.mutate(p.id)}
                  className="px-2 py-0.5 text-xs bg-red-900/40 hover:bg-red-900/60 text-red-400 rounded">
                  ✕
                </button>
              </div>
            </div>
            <div className="flex flex-wrap gap-1.5 text-[10px]">
              {p.nozzleDiameterMm > 0 && <Chip color="violet">Ø{p.nozzleDiameterMm} mm nozzle</Chip>}
              <Chip color="blue">LH {p.layerHeightMm} mm</Chip>
              <Chip color="orange">{p.printSpeedMmS} mm/s</Chip>
              <Chip color="green">Flow {p.materialFlowPct}%</Chip>
              <Chip color="gray">{p.printTemperatureDegC}°C</Chip>
              {p.pelletModeEnabled && <Chip color="amber">Pellet Mode</Chip>}
            </div>
          </div>
        ))}
        {profiles.length === 0 && (
          <p className="col-span-3 text-gray-600 text-sm">No profiles yet — create one to get started.</p>
        )}
      </div>

      {/* Editor modal */}
      {draft && (
        <div className="fixed inset-0 bg-black/60 flex items-center justify-center z-50">
          <div className="bg-gray-900 border border-gray-700 rounded-xl w-full max-w-2xl max-h-[92vh] overflow-y-auto">
            <div className="sticky top-0 bg-gray-900 border-b border-gray-800 px-6 py-4 flex items-center justify-between">
              <h3 className="font-semibold text-white">{draft.id ? 'Edit' : 'New'} Print Profile</h3>
              <div className="flex gap-2">
                <button
                  onClick={() => setAdvanced(a => !a)}
                  className={`px-3 py-1.5 text-xs rounded-lg border transition ${
                    advanced
                      ? 'bg-blue-900/50 border-blue-700 text-blue-300'
                      : 'bg-gray-800 border-gray-700 text-gray-400 hover:text-gray-200'
                  }`}
                >
                  {advanced ? '▲ Basic' : '▼ Advanced'}
                </button>
                <button onClick={applyAllAuto}
                  className="px-3 py-1.5 text-xs bg-emerald-900/50 border border-emerald-700 text-emerald-300 rounded-lg hover:bg-emerald-900/80"
                  title="Restore all Cura-derived defaults for current nozzle diameter"
                >
                  Auto All
                </button>
              </div>
            </div>

            <div className="p-6 space-y-5">
              {/* Profile name */}
              <F label="Profile Name">
                <input className="input w-full" value={draft.name}
                  onChange={e => set('name', e.target.value)} placeholder="My Profile" />
              </F>

              {/* ── BASIC ── */}
              <Section title="Basic Settings"
                subtitle="These four settings directly drive Cura and affect every print">
                <div className="grid grid-cols-2 gap-4">
                  <F label="Nozzle Diameter (mm)" hint="machine_nozzle_size — 0 = use machine default">
                    <WithAuto onAuto={() => { /* nozzle is the source of truth, no auto */ }}>
                      <NumIn value={draft.nozzleDiameterMm ?? 0.4} min={0} max={5} step={0.05}
                        onChange={v => set('nozzleDiameterMm', v)} />
                    </WithAuto>
                  </F>
                  <F label="Layer Height (mm)" hint="layer_height">
                    <WithAuto onAuto={() => applyAuto('layerHeightMm')}>
                      <NumIn value={draft.layerHeightMm ?? 0.2} min={0.05} max={0.8} step={0.05}
                        onChange={v => set('layerHeightMm', v)} />
                    </WithAuto>
                    <AutoHint>{((draft.nozzleDiameterMm ?? 0.4) * 0.5).toFixed(2)} mm (nozzle × 0.5)</AutoHint>
                  </F>
                  <F label="Print Speed (mm/s)" hint="speed_print">
                    <WithAuto onAuto={() => applyAuto('printSpeedMmS')}>
                      <NumIn value={draft.printSpeedMmS ?? 60} min={1} max={500}
                        onChange={v => set('printSpeedMmS', v)} />
                    </WithAuto>
                    <AutoHint>60 mm/s (Cura default)</AutoHint>
                  </F>
                  <F label="Flow / Extrusion (%)" hint="material_flow">
                    <WithAuto onAuto={() => applyAuto('materialFlowPct')}>
                      <NumIn value={draft.materialFlowPct ?? 100} min={50} max={200} step={1}
                        onChange={v => set('materialFlowPct', v)} />
                    </WithAuto>
                    <AutoHint>100 % (Cura default)</AutoHint>
                  </F>
                </div>
              </Section>

              {/* ── PELLET MODE ── */}
              <Section
                title="Pellet Extrusion Mode"
                subtitle="For direct-pellet extruders. Uses a virtual filament diameter so Cura's E-math matches actual pellet volume output."
              >
                <div className="space-y-3">
                  {/* Toggle */}
                  <label className="flex items-center gap-3 cursor-pointer select-none w-fit">
                    <input
                      type="checkbox"
                      className="sr-only"
                      checked={!!draft.pelletModeEnabled}
                      onChange={e => set('pelletModeEnabled', e.target.checked)}
                    />
                    <div className={`relative w-11 h-6 rounded-full transition-colors ${
                      draft.pelletModeEnabled ? 'bg-amber-600' : 'bg-gray-700'
                    }`}>
                      <span className={`absolute top-0.5 left-0.5 w-5 h-5 rounded-full bg-white shadow transition-transform ${
                        draft.pelletModeEnabled ? 'translate-x-5' : 'translate-x-0'
                      }`} />
                    </div>
                    <span className={`text-sm font-medium ${draft.pelletModeEnabled ? 'text-amber-300' : 'text-gray-400'}`}>
                      {draft.pelletModeEnabled ? 'Pellet Mode ON' : 'Pellet Mode OFF (normal filament)'}
                    </span>
                  </label>

                  {/* Pellet sub-settings — only shown when ON */}
                  {draft.pelletModeEnabled && (
                    <div className="pl-3 space-y-3 border-l-2 border-amber-800/60">
                      <div className="grid grid-cols-2 gap-4">
                        <F label="Virtual Filament Diameter (mm)" hint="material_diameter override">
                          <NumIn
                            value={draft.virtualFilamentDiameterMm ?? 1.0}
                            min={0.1} max={5} step={0.05}
                            onChange={v => set('virtualFilamentDiameterMm', v)}
                          />
                          <AutoHint>1.0 mm typical — tune with calibration tests</AutoHint>
                        </F>
                      </div>

                      <div className="bg-amber-950/30 border border-amber-800/40 rounded-lg p-3 text-xs text-amber-300/80 space-y-1.5">
                        <p className="font-medium text-amber-300">How it works</p>
                        <p>
                          Cura computes{' '}
                          <span className="font-mono text-amber-200">E = (b × h × L) / (π/4 × d²) × flow</span>.
                          Setting <span className="font-mono text-amber-200">d = virtual diameter</span> maps
                          this formula to your pellet extruder's actual volumetric output.
                        </p>
                        <p>
                          Start with 1.0 mm, then use the calibration page to find the right value.
                        </p>
                        <Link
                          to="/pellet-calibration"
                          className="inline-block mt-1 px-3 py-1.5 bg-amber-800/50 hover:bg-amber-700/60 border border-amber-700/50 text-amber-200 rounded-lg text-xs font-medium transition"
                        >
                          Open Pellet Calibration →
                        </Link>
                      </div>
                    </div>
                  )}
                </div>
              </Section>

              {/* ── ADVANCED ── */}
              {advanced && (
                <>
                  <Section title="Advanced Speed Settings"
                    subtitle="Fine-tune individual move categories — wired to separate Cura speed_* keys">
                    <div className="grid grid-cols-2 gap-4">
                      <F label="Travel Speed (mm/s)" hint="speed_travel">
                        <WithAuto onAuto={() => applyAuto('travelSpeedMmS')}>
                          <NumIn value={draft.travelSpeedMmS ?? 150} min={1} max={500}
                            onChange={v => set('travelSpeedMmS', v)} />
                        </WithAuto>
                        <AutoHint>150 mm/s (Cura default)</AutoHint>
                      </F>
                      <F label="Outer Wall Speed (mm/s)" hint="speed_wall_0">
                        <WithAuto onAuto={() => applyAuto('wallSpeedMmS')}>
                          <NumIn value={draft.wallSpeedMmS ?? 30} min={1} max={500}
                            onChange={v => set('wallSpeedMmS', v)} />
                        </WithAuto>
                        <AutoHint>{((draft.printSpeedMmS ?? 60) * 0.5).toFixed(0)} mm/s (print × 0.5)</AutoHint>
                      </F>
                      <F label="Inner Wall Speed (mm/s)" hint="speed_wall_x">
                        <WithAuto onAuto={() => applyAuto('innerWallSpeedMmS')}>
                          <NumIn value={draft.innerWallSpeedMmS ?? 60} min={1} max={500}
                            onChange={v => set('innerWallSpeedMmS', v)} />
                        </WithAuto>
                        <AutoHint>{(draft.printSpeedMmS ?? 60).toFixed(0)} mm/s (= print speed)</AutoHint>
                      </F>
                      <F label="Infill Speed (mm/s)" hint="speed_infill">
                        <WithAuto onAuto={() => applyAuto('infillSpeedMmS')}>
                          <NumIn value={draft.infillSpeedMmS ?? 90} min={1} max={500}
                            onChange={v => set('infillSpeedMmS', v)} />
                        </WithAuto>
                        <AutoHint>{((draft.printSpeedMmS ?? 60) * 1.5).toFixed(0)} mm/s (print × 1.5)</AutoHint>
                      </F>
                    </div>
                  </Section>

                  <Section title="Structure">
                    <div className="grid grid-cols-3 gap-4">
                      <F label="Wall Count">
                        <NumIn value={draft.wallCount ?? 3} min={1} max={20}
                          onChange={v => set('wallCount', v)} />
                      </F>
                      <F label="Line Width (mm)" hint="line_width">
                        <WithAuto onAuto={() => applyAuto('lineWidthMm')}>
                          <NumIn value={draft.lineWidthMm ?? 0.4} min={0.1} max={1.5} step={0.05}
                            onChange={v => set('lineWidthMm', v)} />
                        </WithAuto>
                        <AutoHint>{(draft.nozzleDiameterMm ?? 0.4).toFixed(2)} mm (= nozzle)</AutoHint>
                      </F>
                      <F label="First Layer Speed (mm/s)" hint="speed_layer_0">
                        <WithAuto onAuto={() => applyAuto('firstLayerSpeedMmS')}>
                          <NumIn value={draft.firstLayerSpeedMmS ?? 20} min={1} max={100}
                            onChange={v => set('firstLayerSpeedMmS', v)} />
                        </WithAuto>
                        <AutoHint>20 mm/s (Cura default)</AutoHint>
                      </F>
                    </div>
                  </Section>

                  <Section title="Temperature & Cooling">
                    <div className="grid grid-cols-2 gap-4">
                      <F label="Print Temperature (°C)">
                        <NumIn value={draft.printTemperatureDegC ?? 210} min={150} max={350}
                          onChange={v => set('printTemperatureDegC', v)} />
                      </F>
                      <F label="Bed Temperature (°C)">
                        <NumIn value={draft.bedTemperatureDegC ?? 60} min={0} max={150}
                          onChange={v => set('bedTemperatureDegC', v)} />
                      </F>
                      <F label="Cooling Fan Speed (%)">
                        <WithAuto onAuto={() => applyAuto('coolingFanSpeedPct')}>
                          <NumIn value={draft.coolingFanSpeedPct ?? 100} min={0} max={100}
                            onChange={v => set('coolingFanSpeedPct', v)} />
                        </WithAuto>
                      </F>
                      <F label="Retract Length (mm)">
                        <NumIn value={draft.retractLengthMm ?? 5} min={0} max={20} step={0.5}
                          onChange={v => set('retractLengthMm', v)} />
                      </F>
                    </div>
                  </Section>

                  <Section title="Infill">
                    <div className="grid grid-cols-2 gap-4">
                      <F label="Infill Density (%)">
                        <NumIn value={draft.infillDensityPct ?? 20} min={0} max={100}
                          onChange={v => set('infillDensityPct', v)} />
                      </F>
                      <F label="Infill Pattern">
                        <select className="input w-full" value={draft.infillPattern ?? 'grid'}
                          onChange={e => set('infillPattern', e.target.value)}>
                          {['grid','lines','triangles','trihexagon','cubic','concentric','zigzag','cross','gyroid','honeycomb','lightning'].map(p => (
                            <option key={p} value={p}>{p.charAt(0).toUpperCase() + p.slice(1)}</option>
                          ))}
                        </select>
                      </F>
                    </div>
                  </Section>
                </>
              )}
            </div>

            <div className="sticky bottom-0 bg-gray-900 border-t border-gray-800 px-6 py-4 flex justify-between items-center">
              <div className="text-xs text-gray-600">
                {draft.nozzleDiameterMm && draft.nozzleDiameterMm > 0
                  ? `Nozzle Ø${draft.nozzleDiameterMm} mm overrides machine default for slicing`
                  : 'Nozzle = 0: uses machine default nozzle diameter for slicing'}
              </div>
              <div className="flex gap-3">
                <button onClick={() => setDraft(null)}
                  className="px-4 py-2 bg-gray-800 text-gray-300 rounded-lg text-sm">
                  Cancel
                </button>
                <DisabledHint when={!draft.name.trim()} reason="Enter a profile name to save.">
                  <button
                    onClick={save}
                    disabled={!draft.name.trim() || createMutation.isPending || updateMutation.isPending}
                    className="px-4 py-2 bg-primary/80 hover:bg-primary disabled:opacity-40 text-white rounded-lg text-sm"
                  >
                    {(createMutation.isPending || updateMutation.isPending) ? 'Saving…' : 'Save Profile'}
                  </button>
                </DisabledHint>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* Toast notification */}
      {toast && (
        <div className={`fixed bottom-6 left-1/2 -translate-x-1/2 z-50 px-5 py-3 rounded-xl shadow-lg text-sm font-medium transition-all
          ${toast.ok ? 'bg-emerald-900/90 text-emerald-200 border border-emerald-700' : 'bg-red-900/90 text-red-200 border border-red-700'}`}>
          {toast.ok ? '✓' : '✕'} {toast.msg}
        </div>
      )}
    </div>
  )
}

// ── Small helpers ─────────────────────────────────────────────────────────────

function Chip({ children, color }: { children: React.ReactNode; color: string }) {
  const cls: Record<string, string> = {
    violet: 'bg-violet-900/40 text-violet-300',
    blue:   'bg-blue-900/40 text-blue-300',
    orange: 'bg-orange-900/40 text-orange-300',
    green:  'bg-green-900/40 text-green-300',
    gray:   'bg-gray-800 text-gray-400',
    amber:  'bg-amber-900/40 text-amber-300',
  }
  return <span className={`px-1.5 py-0.5 rounded ${cls[color] ?? cls.gray}`}>{children}</span>
}

function Section({ title, subtitle, children }: { title: string; subtitle?: string; children: React.ReactNode }) {
  return (
    <div className="space-y-3">
      <div>
        <p className="text-sm font-medium text-gray-200">{title}</p>
        {subtitle && <p className="text-xs text-gray-500 mt-0.5">{subtitle}</p>}
      </div>
      {children}
    </div>
  )
}

function F({ label, hint, children }: { label: string; hint?: string; children: React.ReactNode }) {
  return (
    <div className="space-y-1">
      <div className="flex items-center gap-1.5">
        <label className="text-sm text-gray-400">{label}</label>
        {hint && <span className="text-[10px] text-gray-600 font-mono">{hint}</span>}
      </div>
      {children}
    </div>
  )
}

function WithAuto({ children, onAuto }: { children: React.ReactNode; onAuto: () => void }) {
  return (
    <div className="flex items-center gap-1.5">
      <div className="flex-1">{children}</div>
      <button onClick={onAuto}
        className="px-2 py-1 text-[10px] bg-emerald-900/40 hover:bg-emerald-900/60 border border-emerald-800 text-emerald-400 rounded whitespace-nowrap"
        title="Restore Cura-derived default for this setting">
        Auto
      </button>
    </div>
  )
}

function AutoHint({ children }: { children: React.ReactNode }) {
  return <p className="text-[10px] text-gray-600 mt-0.5">Auto: {children}</p>
}

function NumIn({ value, min, max, step = 1, onChange }: {
  value: number; min?: number; max?: number; step?: number
  onChange: (v: number) => void
}) {
  const [raw, setRaw] = useState(String(value))
  const ref = useRef<HTMLInputElement>(null)

  // Sync when parent value changes (but not while user is editing)
  useEffect(() => {
    if (document.activeElement !== ref.current) setRaw(String(value))
  }, [value])

  return (
    <input ref={ref} type="number" min={min} max={max} step={step} value={raw}
      onChange={e => {
        setRaw(e.target.value)
        const n = +e.target.value
        if (e.target.value !== '' && !isNaN(n)) onChange(n)
      }}
      onBlur={() => {
        const n = +raw
        if (raw === '' || isNaN(n)) {
          const fb = min ?? 0
          setRaw(String(fb))
          onChange(fb)
        } else {
          setRaw(String(n))
          onChange(n)
        }
      }}
      className="input w-full" />
  )
}
