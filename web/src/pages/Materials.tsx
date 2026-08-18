import { useState } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { materialsApi } from '../api/client'
import DisabledHint from '../components/DisabledHint'
import type { Material } from '../types'

// ── Temperature bar visualization ─────────────────────────────────────────────

function TempBar({ min, max, label, color }: {
  min: number; max: number; label: string; color: string
}) {
  const lo = 0, hi = 400
  const left = ((min - lo) / (hi - lo)) * 100
  const width = ((max - min) / (hi - lo)) * 100
  return (
    <div className="space-y-0.5">
      <div className="flex items-center justify-between text-[10px]">
        <span className="text-gray-500">{label}</span>
        <span className={color}>{min}–{max}°C</span>
      </div>
      <div className="h-1.5 bg-gray-800 rounded-full overflow-hidden relative">
        <div className={`absolute h-full rounded-full ${color === 'text-orange-400' ? 'bg-orange-500/60' : 'bg-blue-500/60'}`}
          style={{ left: `${left}%`, width: `${Math.max(width, 1)}%` }} />
      </div>
    </div>
  )
}

// ── Material card ─────────────────────────────────────────────────────────────

function MaterialCard({ m, onEdit, onDelete }: {
  m: Material; onEdit: () => void; onDelete: () => void
}) {
  const hasBed = m.bedTempMinDegC > 0 || m.bedTempMaxDegC > 0
  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 hover:border-gray-700 transition-colors group">
      <div className="flex items-start justify-between mb-3">
        <div className="flex-1 min-w-0">
          <h3 className="font-semibold text-white truncate">{m.name}</h3>
          <p className="text-xs text-gray-500 mt-0.5">{m.type} · {m.diameterMm} mm filament</p>
        </div>
        <div className="flex gap-1 opacity-0 group-hover:opacity-100 transition-opacity">
          <button onClick={onEdit}
            className="px-2 py-1 text-xs rounded bg-gray-800 hover:bg-gray-700 text-gray-400 hover:text-white transition">
            Edit
          </button>
          <button onClick={() => { if (confirm('Delete this material?')) onDelete() }}
            className="px-2 py-1 text-xs rounded bg-gray-800 hover:bg-red-900/60 text-gray-400 hover:text-red-300 transition">
            Del
          </button>
        </div>
      </div>

      <div className="space-y-2">
        <TempBar min={m.printTempMinDegC} max={m.printTempMaxDegC} label="Nozzle" color="text-orange-400" />
        {hasBed ? (
          <TempBar min={m.bedTempMinDegC} max={m.bedTempMaxDegC} label="Bed" color="text-blue-400" />
        ) : (
          <div className="text-[10px] text-gray-600">No heated bed required</div>
        )}
      </div>
    </div>
  )
}

// ── Form field ────────────────────────────────────────────────────────────────

function MField({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="space-y-1">
      <label className="text-xs font-medium text-gray-400">{label}</label>
      {children}
    </div>
  )
}

function NumInput({
  value, min, max, step = 1, onChange,
}: {
  value: number; min?: number; max?: number; step?: number
  onChange: (v: number) => void
}) {
  return (
    <input type="number" className="input text-sm w-full" value={value} min={min} max={max} step={step}
      onChange={e => { const v = parseFloat(e.target.value); if (!isNaN(v)) onChange(v) }} />
  )
}

// ── Main component ────────────────────────────────────────────────────────────

export default function Materials() {
  const qc = useQueryClient()
  const { data: materials = [] } = useQuery({ queryKey: ['materials'], queryFn: materialsApi.getAll })
  const [form, setForm] = useState<Partial<Material> | null>(null)

  const createMutation = useMutation({
    mutationFn: materialsApi.create,
    onSuccess: () => { qc.invalidateQueries({ queryKey: ['materials'] }); setForm(null) },
  })
  const updateMutation = useMutation({
    mutationFn: ({ id, data }: { id: string; data: any }) => materialsApi.update(id, data),
    onSuccess: () => { qc.invalidateQueries({ queryKey: ['materials'] }); setForm(null) },
  })
  const deleteMutation = useMutation({
    mutationFn: materialsApi.delete,
    onSuccess: () => qc.invalidateQueries({ queryKey: ['materials'] }),
  })

  const openNew = () => {
    setForm({ name: '', type: 'PLA', printTempMinDegC: 200, printTempMaxDegC: 230, bedTempMinDegC: 50, bedTempMaxDegC: 70, diameterMm: 1.75 })
  }

  const set = (k: string, v: string | number) =>
    setForm(f => f ? { ...f, [k]: v } : f)

  const isEditing = !!(form && form.id)
  const bedEnabled = (form?.bedTempMinDegC ?? 0) > 0 || (form?.bedTempMaxDegC ?? 0) > 0

  const handleSave = () => {
    if (!form || !form.name?.trim()) return
    const payload = {
      name: form.name,
      type: form.type,
      printTempMin: form.printTempMinDegC,
      printTempMax: form.printTempMaxDegC,
      bedTempMin: form.bedTempMinDegC,
      bedTempMax: form.bedTempMaxDegC,
      diameterMm: form.diameterMm,
    } as any
    if (isEditing) {
      updateMutation.mutate({ id: form.id!, data: payload })
    } else {
      createMutation.mutate(payload)
    }
  }

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <div>
          <h2 className="text-2xl font-semibold text-white">Materials</h2>
          <p className="text-sm text-gray-500 mt-0.5">
            {materials.length} material{materials.length !== 1 ? 's' : ''} defined
            {materials.length > 0 && ' — temperatures are used during slicing'}
          </p>
        </div>
        <button onClick={openNew}
          className="px-4 py-2 bg-primary/80 hover:bg-primary text-white text-sm rounded-lg transition">
          + Add Material
        </button>
      </div>

      {materials.length > 0 ? (
        <div className="grid grid-cols-1 md:grid-cols-2 xl:grid-cols-3 gap-4">
          {materials.map(m => (
            <MaterialCard
              key={m.id}
              m={m}
              onEdit={() => setForm({ ...m })}
              onDelete={() => deleteMutation.mutate(m.id)}
            />
          ))}
        </div>
      ) : (
        <div className="text-center py-16 bg-gray-900/50 border border-gray-800 rounded-xl">
          <div className="text-4xl text-gray-700 mb-3">No materials yet</div>
          <p className="text-gray-500 text-sm">Click <span className="text-primary/80 font-medium">+ Add Material</span> to define your first material profile.</p>
        </div>
      )}

      {/* ── Add / Edit modal ── */}
      {form && (
        <div className="fixed inset-0 bg-black/60 backdrop-blur-sm flex items-center justify-center z-50 p-4"
          onClick={e => { if (e.target === e.currentTarget) setForm(null) }}>
          <div className="bg-gray-900 border border-gray-700 rounded-xl p-6 w-full max-w-lg space-y-4 shadow-2xl">
            <div className="flex items-center justify-between">
              <h3 className="font-semibold text-white text-lg">
                {isEditing ? `Edit: ${form.name}` : 'New Material'}
              </h3>
              <button onClick={() => setForm(null)}
                className="text-gray-500 hover:text-gray-300 transition text-lg leading-none px-1">&times;</button>
            </div>

            {/* Name + Type */}
            <div className="grid grid-cols-2 gap-3">
              <MField label="Name">
                <input className="input w-full" value={form.name ?? ''} onChange={e => set('name', e.target.value)} placeholder="My PLA" />
              </MField>
              <MField label="Type">
                <input className="input w-full" value={form.type ?? ''} onChange={e => set('type', e.target.value)} placeholder="PLA" />
              </MField>
            </div>

            {/* Nozzle Temperature */}
            <div>
              <p className="text-[10px] text-gray-500 uppercase tracking-wider mb-2 font-medium">Nozzle Temperature</p>
              <div className="grid grid-cols-2 gap-3">
                <MField label="Min (°C)">
                  <NumInput value={form.printTempMinDegC ?? 200} min={100} max={400} onChange={v => set('printTempMinDegC', v)} />
                </MField>
                <MField label="Max (°C)">
                  <NumInput value={form.printTempMaxDegC ?? 230} min={100} max={400} onChange={v => set('printTempMaxDegC', v)} />
                </MField>
              </div>
              <p className="text-[10px] text-gray-600 mt-1">The max temperature is used during slicing.</p>
            </div>

            {/* Bed Temperature */}
            <div>
              <div className="flex items-center justify-between mb-2">
                <p className="text-[10px] text-gray-500 uppercase tracking-wider font-medium">Bed Temperature</p>
                <button
                  onClick={() => {
                    if (bedEnabled) { set('bedTempMinDegC', 0); set('bedTempMaxDegC', 0) }
                    else { set('bedTempMinDegC', 50); set('bedTempMaxDegC', 70) }
                  }}
                  className={`relative w-9 h-5 rounded-full transition-colors flex-shrink-0 ${
                    bedEnabled ? 'bg-primary' : 'bg-gray-600'
                  }`}
                >
                  <span className={`absolute top-0.5 left-0.5 w-4 h-4 rounded-full bg-white transition-transform ${
                    bedEnabled ? 'translate-x-4' : ''
                  }`} />
                </button>
              </div>
              {bedEnabled && (
                <div className="grid grid-cols-2 gap-3">
                  <MField label="Min (°C)">
                    <NumInput value={form.bedTempMinDegC ?? 50} min={1} max={150} onChange={v => set('bedTempMinDegC', v)} />
                  </MField>
                  <MField label="Max (°C)">
                    <NumInput value={form.bedTempMaxDegC ?? 70} min={1} max={150} onChange={v => set('bedTempMaxDegC', v)} />
                  </MField>
                </div>
              )}
              {!bedEnabled && (
                <p className="text-[10px] text-gray-600">No heated bed required for this material.</p>
              )}
            </div>

            {/* Filament */}
            <div className="grid grid-cols-2 gap-3">
              <MField label="Filament Diameter (mm)">
                <NumInput value={form.diameterMm ?? 1.75} min={0.5} max={5} step={0.05} onChange={v => set('diameterMm', v)} />
              </MField>
            </div>

            {/* Actions */}
            <div className="flex gap-3 justify-end pt-2 border-t border-gray-800">
              <button onClick={() => setForm(null)}
                className="px-4 py-2.5 bg-gray-800 text-gray-300 rounded-lg text-sm hover:bg-gray-700 transition">Cancel</button>
              <DisabledHint when={!form.name?.trim()} reason="Enter a material name to save.">
                <button onClick={handleSave}
                  disabled={!form.name?.trim() || createMutation.isPending || updateMutation.isPending}
                  className="px-6 py-2.5 bg-primary/80 hover:bg-primary disabled:opacity-40 text-white rounded-lg text-sm font-medium transition">
                  {(createMutation.isPending || updateMutation.isPending) ? 'Saving...' : isEditing ? 'Update' : 'Create'}
                </button>
              </DisabledHint>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}
