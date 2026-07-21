import { useState } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { materialsApi } from '../api/client'
import DisabledHint from '../components/DisabledHint'
import type { Material } from '../types'

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
    onSuccess: () => { qc.invalidateQueries({ queryKey: ['materials'] }) },
  })

  const openNew = () => {
    setForm({ name: '', type: 'PLA', printTempMinDegC: 200, printTempMaxDegC: 230, bedTempMinDegC: 50, bedTempMaxDegC: 70, diameterMm: 1.75 })
  }

  const openEdit = (m: Material) => {
    setForm({ ...m })
  }

  const set = (k: string, v: string | number) =>
    setForm(f => f ? { ...f, [k]: v } : f)

  const isEditing = !!(form && form.id)

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
        <h2 className="text-2xl font-semibold text-white">Materials</h2>
        <button onClick={openNew}
          className="px-4 py-2 bg-primary/80 hover:bg-primary text-white text-sm rounded-lg">
          + Add Material
        </button>
      </div>

      <div className="grid gap-4">
        {materials.map(m => (
          <div key={m.id} className="bg-gray-900 border border-gray-800 rounded-xl p-5 flex items-center justify-between">
            <div>
              <p className="font-medium text-white">{m.name}</p>
              <p className="text-xs text-gray-500 mt-0.5">
                {m.type} · Filament Ø{m.diameterMm} mm ·
                Print {m.printTempMinDegC}–{m.printTempMaxDegC}°C ·
                {m.bedTempMinDegC > 0 || m.bedTempMaxDegC > 0 ? `Bed ${m.bedTempMinDegC}–${m.bedTempMaxDegC}°C` : 'No heated bed'}
              </p>
            </div>
            <div className="flex gap-2">
              <button onClick={() => openEdit(m)}
                className="text-sm text-gray-400 hover:text-blue-400 px-3 py-1 rounded bg-gray-800">Edit</button>
              <button onClick={() => { if (confirm('Delete this material?')) deleteMutation.mutate(m.id) }}
                className="text-sm text-gray-400 hover:text-red-400 px-3 py-1 rounded bg-gray-800">Delete</button>
            </div>
          </div>
        ))}
        {materials.length === 0 && (
          <p className="text-gray-500 text-sm text-center py-8">No materials. Click "+ Add Material" to create one.</p>
        )}
      </div>

      {form && (
        <div className="fixed inset-0 bg-black/60 flex items-center justify-center z-50">
          <div className="bg-gray-900 border border-gray-700 rounded-xl p-6 w-full max-w-lg space-y-4">
            <h3 className="font-semibold text-white">{isEditing ? 'Edit Material' : 'Add Material'}</h3>
            <div className="grid grid-cols-2 gap-3">
              <MField label="Name">
                <input className="input w-full" value={form.name ?? ''} onChange={e => set('name', e.target.value)} placeholder="My PLA" />
              </MField>
              <MField label="Type">
                <input className="input w-full" value={form.type ?? ''} onChange={e => set('type', e.target.value)} placeholder="PLA" />
              </MField>
            </div>
            <div className="grid grid-cols-2 gap-3">
              <MField label="Print Temp Min (°C)">
                <NumInput value={form.printTempMinDegC ?? 200} min={100} max={400} onChange={v => set('printTempMinDegC', v)} />
              </MField>
              <MField label="Print Temp Max (°C)">
                <NumInput value={form.printTempMaxDegC ?? 230} min={100} max={400} onChange={v => set('printTempMaxDegC', v)} />
              </MField>
            </div>
            <div className="grid grid-cols-3 gap-3">
              <MField label="Heated Bed">
                <div className="flex items-center gap-3 h-9">
                  <button
                    onClick={() => {
                      const isOn = (form.bedTempMinDegC ?? 0) > 0 || (form.bedTempMaxDegC ?? 0) > 0
                      if (isOn) { set('bedTempMinDegC', 0); set('bedTempMaxDegC', 0) }
                      else { set('bedTempMinDegC', 50); set('bedTempMaxDegC', 70) }
                    }}
                    className={`relative w-10 h-5 rounded-full transition-colors flex-shrink-0 ${
                      (form.bedTempMinDegC ?? 0) > 0 || (form.bedTempMaxDegC ?? 0) > 0 ? 'bg-primary' : 'bg-gray-600'
                    }`}
                  >
                    <span className={`absolute top-0.5 left-0.5 w-4 h-4 rounded-full bg-white transition-transform ${
                      (form.bedTempMinDegC ?? 0) > 0 || (form.bedTempMaxDegC ?? 0) > 0 ? 'translate-x-5' : ''
                    }`} />
                  </button>
                  <span className="text-sm text-gray-400">{(form.bedTempMinDegC ?? 0) > 0 || (form.bedTempMaxDegC ?? 0) > 0 ? 'Enabled' : 'Disabled'}</span>
                </div>
              </MField>
              {((form.bedTempMinDegC ?? 0) > 0 || (form.bedTempMaxDegC ?? 0) > 0) && (
                <>
                  <MField label="Bed Temp Min (°C)">
                    <NumInput value={form.bedTempMinDegC ?? 50} min={1} max={150} onChange={v => set('bedTempMinDegC', v)} />
                  </MField>
                  <MField label="Bed Temp Max (°C)">
                    <NumInput value={form.bedTempMaxDegC ?? 70} min={1} max={150} onChange={v => set('bedTempMaxDegC', v)} />
                  </MField>
                </>
              )}
              <MField label="Filament Diameter (mm)">
                <NumInput value={form.diameterMm ?? 1.75} min={0.5} max={5} step={0.05} onChange={v => set('diameterMm', v)} />
              </MField>
            </div>
            <div className="flex gap-3 justify-end pt-2">
              <button onClick={() => setForm(null)}
                className="px-4 py-2 bg-gray-800 text-gray-300 rounded-lg text-sm">Cancel</button>
              <DisabledHint when={!form.name?.trim()} reason="Enter a material name to save.">
                <button onClick={handleSave}
                  disabled={!form.name?.trim() || createMutation.isPending || updateMutation.isPending}
                  className="px-4 py-2 bg-primary/80 hover:bg-primary disabled:opacity-40 text-white rounded-lg text-sm">
                  {(createMutation.isPending || updateMutation.isPending) ? 'Saving…' : 'Save'}
                </button>
              </DisabledHint>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}

function MField({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="space-y-1">
      <label className="text-sm text-gray-400">{label}</label>
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
