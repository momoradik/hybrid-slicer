import { useState } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { toolsApi } from '../api/client'
import DisabledHint from '../components/DisabledHint'
import NumericInput from '../components/NumericInput'
import type { CncTool, ToolType } from '../types'

const TOOL_TYPES: { value: ToolType; label: string; icon: string }[] = [
  { value: 'FlatEndMill',    label: 'Flat End Mill',     icon: '|' },
  { value: 'BallEndMill',    label: 'Ball End Mill',     icon: ')' },
  { value: 'BullNoseEndMill',label: 'Bull Nose End Mill',icon: 'U' },
  { value: 'DrillBit',       label: 'Drill Bit',         icon: 'V' },
  { value: 'Engraver',       label: 'Engraver',          icon: '/' },
  { value: 'Facemill',       label: 'Face Mill',         icon: 'T' },
  { value: 'Custom',         label: 'Custom',            icon: '*' },
]

function toolTypeLabel(t: string) {
  return TOOL_TYPES.find(tt => tt.value === t)?.label ?? t
}

// ── Interactive tool diagram that reflects current dimensions ──────────────────

function ToolDiagram({ diameter, fluteLength, toolLength, shankDiameter, type }: {
  diameter: number; fluteLength: number; toolLength: number
  shankDiameter: number; type: string
}) {
  const W = 220, H = 340
  const cx = W / 2

  // Normalize dimensions for rendering (scale to fit 240px height)
  const maxDim = Math.max(toolLength || 50, 50)
  const scale = 220 / maxDim
  const tl = (toolLength || 50) * scale
  const fl = Math.min(fluteLength || 20, toolLength || 50) * scale
  const diam = Math.max((diameter || 3) * scale, 14)
  const shankD = Math.max((shankDiameter || diameter * 0.8 || 3) * scale, 10)

  // Vertical positions (top = spindle, bottom = tip)
  const topY = (H - tl) / 2 + 10
  const shankEnd = topY + (tl - fl)
  const tipY = topY + tl

  // Tip shape based on tool type
  const tipPath = type === 'BallEndMill'
    ? `M${cx - diam / 2},${shankEnd + fl * 0.85} Q${cx - diam / 2},${tipY} ${cx},${tipY} Q${cx + diam / 2},${tipY} ${cx + diam / 2},${shankEnd + fl * 0.85}`
    : type === 'DrillBit' || type === 'Engraver'
    ? `M${cx - diam / 2},${tipY - 8} L${cx},${tipY} L${cx + diam / 2},${tipY - 8}`
    : '' // flat end — no special tip

  return (
    <svg viewBox={`0 0 ${W} ${H}`} className="w-full" aria-label="Tool dimensions preview">
      {/* Shank */}
      <rect x={cx - shankD / 2} y={topY} width={shankD} height={Math.max(tl - fl, 4)}
        fill="#4b5563" stroke="#6b7280" strokeWidth="1" rx="1" />

      {/* Fluted section */}
      <rect x={cx - diam / 2} y={shankEnd} width={diam} height={fl}
        fill="#374151" stroke="#6b7280" strokeWidth="1" rx="1" />
      {/* Helix lines */}
      {Array.from({ length: Math.max(2, Math.round(fl / 18)) }, (_, i) => {
        const y = shankEnd + (i + 0.5) * (fl / Math.max(2, Math.round(fl / 18)))
        return y < tipY - 4 ? (
          <line key={i} x1={cx - diam / 2 + 2} y1={y} x2={cx + diam / 2 - 2} y2={y - 3}
            stroke="#6b7280" strokeWidth="1" opacity="0.6" />
        ) : null
      })}

      {/* Tip shape */}
      {tipPath && <path d={tipPath} fill="#4b5563" stroke="#6b7280" strokeWidth="1" />}

      {/* ── Dimension annotations ── */}

      {/* Tool Length — right side, blue */}
      <line x1={cx + diam / 2 + 20} y1={topY} x2={cx + diam / 2 + 20} y2={tipY}
        stroke="#3b82f6" strokeWidth="1" strokeDasharray="3,2" />
      <line x1={cx + diam / 2 + 16} y1={topY} x2={cx + diam / 2 + 24} y2={topY} stroke="#3b82f6" strokeWidth="1" />
      <line x1={cx + diam / 2 + 16} y1={tipY} x2={cx + diam / 2 + 24} y2={tipY} stroke="#3b82f6" strokeWidth="1" />
      <text x={cx + diam / 2 + 30} y={(topY + tipY) / 2 + 3} fill="#3b82f6" fontSize="9"
        fontFamily="ui-sans-serif,sans-serif">{toolLength || '?'}</text>

      {/* Flute Length — left side, orange */}
      {fluteLength > 0 && <>
        <line x1={cx - diam / 2 - 20} y1={shankEnd} x2={cx - diam / 2 - 20} y2={tipY}
          stroke="#f97316" strokeWidth="1" strokeDasharray="3,2" />
        <line x1={cx - diam / 2 - 24} y1={shankEnd} x2={cx - diam / 2 - 16} y2={shankEnd} stroke="#f97316" strokeWidth="1" />
        <line x1={cx - diam / 2 - 24} y1={tipY} x2={cx - diam / 2 - 16} y2={tipY} stroke="#f97316" strokeWidth="1" />
        <text x={cx - diam / 2 - 26} y={(shankEnd + tipY) / 2 + 3} fill="#f97316" fontSize="9"
          fontFamily="ui-sans-serif,sans-serif" textAnchor="end">{fluteLength}</text>
      </>}

      {/* Diameter — bottom, purple */}
      <line x1={cx - diam / 2} y1={tipY + 14} x2={cx + diam / 2} y2={tipY + 14}
        stroke="#a78bfa" strokeWidth="1.5" />
      <line x1={cx - diam / 2} y1={tipY + 10} x2={cx - diam / 2} y2={tipY + 18} stroke="#a78bfa" strokeWidth="1" />
      <line x1={cx + diam / 2} y1={tipY + 10} x2={cx + diam / 2} y2={tipY + 18} stroke="#a78bfa" strokeWidth="1" />
      <text x={cx} y={tipY + 28} fill="#a78bfa" fontSize="10" textAnchor="middle"
        fontFamily="ui-sans-serif,sans-serif">{diameter || '?'} mm</text>
    </svg>
  )
}

// ── Tool card for the grid ────────────────────────────────────────────────────

function ToolCard({ tool, onEdit, onDelete }: {
  tool: CncTool; onEdit: () => void; onDelete: () => void
}) {
  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 hover:border-gray-700 transition-colors group">
      <div className="flex items-start justify-between mb-3">
        <div className="flex-1 min-w-0">
          <h3 className="font-semibold text-white truncate">{tool.name}</h3>
          <p className="text-xs text-gray-500 mt-0.5">{toolTypeLabel(tool.type)} · {tool.toolMaterial}</p>
        </div>
        <div className="flex gap-1 opacity-0 group-hover:opacity-100 transition-opacity">
          <button onClick={onEdit}
            className="px-2 py-1 text-xs rounded bg-gray-800 hover:bg-gray-700 text-gray-400 hover:text-white transition">
            Edit
          </button>
          <button onClick={() => { if (confirm('Delete this tool?')) onDelete() }}
            className="px-2 py-1 text-xs rounded bg-gray-800 hover:bg-red-900/60 text-gray-400 hover:text-red-300 transition">
            Del
          </button>
        </div>
      </div>

      {/* Key specs grid */}
      <div className="grid grid-cols-3 gap-2">
        <Spec label="Diameter" value={`${tool.diameterMm}`} unit="mm" color="text-violet-400" />
        <Spec label="Flute L" value={`${tool.fluteLengthMm}`} unit="mm" color="text-orange-400" />
        <Spec label="Tool L" value={`${tool.toolLengthMm ?? '—'}`} unit="mm" color="text-blue-400" />
        <Spec label="Flutes" value={`${tool.fluteCount}`} />
        <Spec label="RPM" value={tool.recommendedRpm.toLocaleString()} />
        <Spec label="Feed" value={`${tool.recommendedFeedMmPerMin}`} unit="mm/m" />
      </div>
    </div>
  )
}

function Spec({ label, value, unit, color }: {
  label: string; value: string; unit?: string; color?: string
}) {
  return (
    <div className="bg-gray-800/50 rounded-lg px-2 py-1.5">
      <div className="text-[10px] text-gray-500 leading-tight">{label}</div>
      <div className={`text-sm font-mono font-medium leading-tight ${color ?? 'text-gray-300'}`}>
        {value}{unit && <span className="text-[10px] text-gray-500 ml-0.5">{unit}</span>}
      </div>
    </div>
  )
}

// ── Labelled form field ───────────────────────────────────────────────────────

function FormField({
  label, labelColor = 'text-gray-400', tooltip, children,
}: {
  label: string; labelColor?: string; tooltip?: string; children: React.ReactNode
}) {
  return (
    <div className="space-y-1">
      <label className={`flex items-center gap-1 text-xs font-medium ${labelColor}`}>
        {label}
        {tooltip && (
          <span title={tooltip} className="cursor-help text-gray-600 hover:text-gray-400 text-[10px]">?</span>
        )}
      </label>
      {children}
    </div>
  )
}

// ── Main component ────────────────────────────────────────────────────────────

export default function ToolLibrary() {
  const qc = useQueryClient()
  const { data: tools = [] } = useQuery({ queryKey: ['tools'], queryFn: toolsApi.getAll })
  const [editing, setEditing] = useState<Partial<CncTool> | null>(null)

  const createMutation = useMutation({
    mutationFn: toolsApi.create,
    onSuccess: () => { qc.invalidateQueries({ queryKey: ['tools'] }); setEditing(null) },
  })
  const updateMutation = useMutation({
    mutationFn: ({ id, data }: { id: string; data: Partial<CncTool> }) => toolsApi.update(id, data),
    onSuccess: () => { qc.invalidateQueries({ queryKey: ['tools'] }); setEditing(null) },
  })
  const deleteMutation = useMutation({
    mutationFn: toolsApi.delete,
    onSuccess: () => qc.invalidateQueries({ queryKey: ['tools'] }),
  })

  const isEditing = !!(editing?.id)
  const fluteExceedsLength =
    editing != null &&
    (editing.fluteLengthMm ?? 0) > 0 &&
    (editing.toolLengthMm ?? 0) > 0 &&
    (editing.fluteLengthMm ?? 0) > (editing.toolLengthMm ?? 0)

  const [search, setSearch] = useState('')

  const filteredTools = tools.filter(t => {
    if (!search) return true
    const q = search.toLowerCase()
    return t.name.toLowerCase().includes(q)
      || t.type.toLowerCase().includes(q)
      || t.toolMaterial.toLowerCase().includes(q)
      || `${t.diameterMm}`.includes(q)
  })

  const set = (k: string, v: string | number) =>
    setEditing(e => e ? { ...e, [k]: v } : e)

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <div>
          <h2 className="text-2xl font-semibold text-white">CNC Tool Library</h2>
          <p className="text-sm text-gray-500 mt-0.5">{tools.length} tool{tools.length !== 1 ? 's' : ''} defined</p>
        </div>
        <button
          onClick={() => setEditing({
            type: 'FlatEndMill', fluteCount: 2, toolMaterial: 'Carbide',
            recommendedRpm: 10000, recommendedFeedMmPerMin: 500, toolLengthMm: 50,
            diameterMm: 3, fluteLengthMm: 12, shankDiameterMm: 3, maxDepthOfCutMm: 0.5,
          })}
          className="px-4 py-2 bg-primary/80 hover:bg-primary text-white text-sm rounded-lg transition"
        >
          + Add Tool
        </button>
      </div>

      {/* Search */}
      {tools.length > 2 && (
        <input className="input w-full max-w-sm" placeholder="Search by name, type, material, or diameter..."
          value={search} onChange={e => setSearch(e.target.value)} />
      )}

      {/* Tool cards grid */}
      {filteredTools.length > 0 ? (
        <div className="grid grid-cols-1 md:grid-cols-2 xl:grid-cols-3 gap-4">
          {filteredTools.map(t => (
            <ToolCard
              key={t.id}
              tool={t}
              onEdit={() => setEditing({ ...t })}
              onDelete={() => deleteMutation.mutate(t.id)}
            />
          ))}
        </div>
      ) : tools.length === 0 ? (
        <div className="text-center py-16 bg-gray-900/50 border border-gray-800 rounded-xl">
          <div className="text-4xl text-gray-700 mb-3">No tools yet</div>
          <p className="text-gray-500 text-sm">Click <span className="text-primary/80 font-medium">+ Add Tool</span> to define your first CNC tool.</p>
        </div>
      ) : (
        <p className="text-gray-500 text-sm text-center py-8">No tools match "{search}"</p>
      )}

      {/* ── Add / Edit modal ── */}
      {editing && (
        <div className="fixed inset-0 bg-black/60 backdrop-blur-sm flex items-start justify-center z-50 p-4 overflow-y-auto"
          onClick={e => { if (e.target === e.currentTarget) setEditing(null) }}>
          <div className="bg-gray-900 border border-gray-700 rounded-xl p-6 w-full max-w-3xl space-y-5 my-8 shadow-2xl">
            <div className="flex items-center justify-between">
              <h3 className="font-semibold text-white text-lg">
                {isEditing ? `Edit: ${editing.name}` : 'New CNC Tool'}
              </h3>
              <button onClick={() => setEditing(null)}
                className="text-gray-500 hover:text-gray-300 transition text-lg leading-none px-1">&times;</button>
            </div>

            {/* Two-column layout: diagram (left) + form (right) */}
            <div className="flex gap-6">

              {/* ── Interactive Diagram ── */}
              <div className="flex-shrink-0 w-48 bg-gray-950/60 rounded-xl border border-gray-800 p-3 flex flex-col items-center">
                <p className="text-[10px] text-gray-500 mb-1 text-center uppercase tracking-wider">Live Preview</p>
                <ToolDiagram
                  diameter={editing.diameterMm ?? 3}
                  fluteLength={editing.fluteLengthMm ?? 0}
                  toolLength={editing.toolLengthMm ?? 50}
                  shankDiameter={editing.shankDiameterMm ?? 0}
                  type={editing.type ?? 'FlatEndMill'}
                />
                {/* Colour legend */}
                <div className="flex flex-col gap-1 text-[10px] w-full mt-2 pt-2 border-t border-gray-800">
                  <div className="flex items-center gap-1.5">
                    <span className="inline-block w-3 h-0.5 bg-violet-400 rounded" />
                    <span className="text-violet-400">Diameter</span>
                  </div>
                  <div className="flex items-center gap-1.5">
                    <span className="inline-block w-3 h-0.5 bg-orange-500 rounded" />
                    <span className="text-orange-400">Flute Length</span>
                  </div>
                  <div className="flex items-center gap-1.5">
                    <span className="inline-block w-3 h-0.5 bg-blue-500 rounded" />
                    <span className="text-blue-400">Tool Length</span>
                  </div>
                </div>
              </div>

              {/* ── Form fields ── */}
              <div className="flex-1 space-y-4">
                {/* Name + Type */}
                <div className="grid grid-cols-2 gap-3">
                  <FormField label="Tool Name">
                    <input className="input w-full" placeholder="e.g. 3mm 2-Flute Carbide"
                      value={editing.name ?? ''}
                      onChange={e => set('name', e.target.value)} />
                  </FormField>
                  <FormField label="Tool Type">
                    <select className="input w-full" value={editing.type}
                      onChange={e => set('type', e.target.value)}>
                      {TOOL_TYPES.map(t => <option key={t.value} value={t.value}>{t.label}</option>)}
                    </select>
                  </FormField>
                </div>

                {/* Geometry */}
                <div>
                  <p className="text-[10px] text-gray-500 uppercase tracking-wider mb-2 font-medium">Geometry</p>
                  <div className="grid grid-cols-3 gap-3">
                    <FormField label="Diameter (mm)" labelColor="text-violet-400"
                      tooltip="Cutting diameter — sets milling pass width and cutter-radius compensation.">
                      <NumericInput min={0.1} step={0.1} value={editing.diameterMm ?? 3} onChange={v => set('diameterMm', v)} />
                    </FormField>
                    <FormField label="Flute Length (mm)" labelColor="text-orange-400"
                      tooltip="Length of cutting edges. Sets maximum axial depth of cut.">
                      <NumericInput min={0.5} step={0.5} value={editing.fluteLengthMm ?? 12} onChange={v => set('fluteLengthMm', v)} />
                    </FormField>
                    <FormField label="Tool Length (mm)" labelColor="text-blue-400"
                      tooltip="Spindle collet to tip. Used for clearance safety checks.">
                      <NumericInput min={1} step={1} value={editing.toolLengthMm ?? 50} onChange={v => set('toolLengthMm', v)} />
                    </FormField>
                    <FormField label="Shank Diameter (mm)"
                      tooltip="Non-cutting portion held in collet. Used for pocket-access checks.">
                      <NumericInput min={0} step={0.1} value={editing.shankDiameterMm ?? 3} onChange={v => set('shankDiameterMm', v)} />
                    </FormField>
                    <FormField label="Flute Count" tooltip="Number of cutting edges.">
                      <NumericInput min={1} max={12} step={1} value={editing.fluteCount ?? 2} onChange={v => set('fluteCount', v)} />
                    </FormField>
                    <FormField label="Material" tooltip="Tool material (HSS, Carbide, Cobalt).">
                      <input className="input w-full" placeholder="Carbide"
                        value={editing.toolMaterial ?? ''} onChange={e => set('toolMaterial', e.target.value)} />
                    </FormField>
                  </div>
                </div>

                {/* Validation warning */}
                {fluteExceedsLength && (
                  <div className="bg-red-950/40 border border-red-700/50 rounded-lg px-3 py-2 text-xs text-red-400">
                    Flute length ({editing.fluteLengthMm} mm) exceeds tool length ({editing.toolLengthMm} mm).
                  </div>
                )}

                {/* Cutting parameters */}
                <div>
                  <p className="text-[10px] text-gray-500 uppercase tracking-wider mb-2 font-medium">Cutting Parameters</p>
                  <div className="grid grid-cols-3 gap-3">
                    <FormField label="Max Depth of Cut (mm)" tooltip="Maximum axial depth per pass.">
                      <NumericInput min={0} step={0.1} value={editing.maxDepthOfCutMm ?? 0.5} onChange={v => set('maxDepthOfCutMm', v)} />
                    </FormField>
                    <FormField label="Spindle RPM" tooltip="Recommended speed. Used in M3 S command.">
                      <NumericInput min={100} step={500} value={editing.recommendedRpm ?? 10000} onChange={v => set('recommendedRpm', v)} />
                    </FormField>
                    <FormField label="Feed Rate (mm/min)" tooltip="Recommended XY feed rate during cutting.">
                      <NumericInput min={10} step={10} value={editing.recommendedFeedMmPerMin ?? 500} onChange={v => set('recommendedFeedMmPerMin', v)} />
                    </FormField>
                  </div>
                </div>

                {/* Clearance info */}
                {(editing.toolLengthMm ?? 0) > 0 && (
                  <div className="bg-blue-950/20 border border-blue-800/30 rounded-lg px-3 py-2 text-xs text-blue-300/80">
                    Spindle clearance: collet face is <span className="text-blue-200 font-semibold">{(editing.toolLengthMm ?? 50).toFixed(1)} mm</span> above tip.
                    {editing.toolLengthMm && editing.fluteLengthMm && editing.toolLengthMm >= editing.fluteLengthMm && (
                      <span className="text-green-400/80 ml-1">
                        Shank clearance: {((editing.toolLengthMm ?? 0) - (editing.fluteLengthMm ?? 0)).toFixed(1)} mm.
                      </span>
                    )}
                  </div>
                )}
              </div>
            </div>

            {/* Actions */}
            <div className="flex gap-3 justify-end pt-1 border-t border-gray-800">
              <button onClick={() => setEditing(null)}
                className="px-4 py-2.5 bg-gray-800 text-gray-300 rounded-lg text-sm hover:bg-gray-700 transition">
                Cancel
              </button>
              <DisabledHint when={!editing.name || !editing.diameterMm || fluteExceedsLength} reason={
                !editing.name ? 'Enter a tool name.' :
                !editing.diameterMm ? 'Enter a tool diameter.' :
                'Flute length cannot exceed overall tool length.'
              }>
                <button
                  onClick={() => {
                    if (editing.id) updateMutation.mutate({ id: editing.id, data: editing })
                    else createMutation.mutate(editing)
                  }}
                  disabled={!editing.name || !editing.diameterMm || fluteExceedsLength}
                  className="px-6 py-2.5 bg-primary/80 hover:bg-primary disabled:opacity-40 text-white rounded-lg text-sm font-medium transition"
                >
                  {isEditing ? 'Update Tool' : 'Create Tool'}
                </button>
              </DisabledHint>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}
