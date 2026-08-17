import { useState } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { toolsApi } from '../api/client'
import DisabledHint from '../components/DisabledHint'
import type { CncTool, ToolType } from '../types'

const TOOL_TYPES: ToolType[] = ['FlatEndMill', 'BallEndMill', 'BullNoseEndMill', 'DrillBit', 'Engraver', 'Facemill', 'Custom']

// ── Tool anatomy SVG diagram ─────────────────────────────────────────────────

function ToolDiagram() {
  return (
    <div className="flex flex-col items-center gap-2">
      <svg viewBox="0 0 200 390" className="w-full max-w-[170px]" aria-label="Tool anatomy diagram">
        {/* ── Spindle / holder (trapezoid) ── */}
        <path d="M44,12 L156,12 L140,56 L60,56 Z" fill="#374151" stroke="#4b5563" strokeWidth="1.5"/>
        <text x="100" y="39" textAnchor="middle" fill="#9ca3af" fontSize="11" fontFamily="ui-sans-serif,sans-serif">Spindle</text>

        {/* ── Shank ── */}
        <rect x="71" y="56" width="58" height="102" fill="#4b5563" stroke="#6b7280" strokeWidth="1"/>
        <text x="100" y="109" textAnchor="middle" fill="#9ca3af" fontSize="9" fontFamily="ui-sans-serif,sans-serif">Shank</text>

        {/* ── Fluted section ── */}
        <rect x="65" y="158" width="70" height="116" fill="#374151" stroke="#6b7280" strokeWidth="1"/>
        {/* helix lines suggesting flutes */}
        {[0,1,2,3,4].map(i => (
          <line key={i} x1="65" y1={172 + i*22} x2="135" y2={167 + i*22}
            stroke="#6b7280" strokeWidth="1.5"/>
        ))}
        <text x="100" y="218" textAnchor="middle" fill="#9ca3af" fontSize="9" fontFamily="ui-sans-serif,sans-serif">cutting</text>
        <text x="100" y="230" textAnchor="middle" fill="#9ca3af" fontSize="9" fontFamily="ui-sans-serif,sans-serif">edges</text>

        {/* ── Tip (triangle) ── */}
        <polygon points="65,274 135,274 100,302" fill="#4b5563" stroke="#6b7280" strokeWidth="1"/>

        {/* ════ ANNOTATIONS ════ */}

        {/* Tool Length — right side, blue */}
        <line x1="150" y1="12" x2="150" y2="302" stroke="#3b82f6" strokeWidth="1.2" strokeDasharray="4,3"/>
        <polygon points="147,12 153,12 150,6"   fill="#3b82f6"/>
        <polygon points="147,302 153,302 150,308" fill="#3b82f6"/>
        <text x="170" y="157" textAnchor="middle" fill="#3b82f6" fontSize="9.5"
          fontFamily="ui-sans-serif,sans-serif"
          transform="rotate(90,170,157)">Tool Length</text>

        {/* Flute Length — left side, orange */}
        <line x1="50" y1="158" x2="50" y2="302" stroke="#f97316" strokeWidth="1.2" strokeDasharray="4,3"/>
        <polygon points="47,158 53,158 50,152" fill="#f97316"/>
        <polygon points="47,302 53,302 50,308" fill="#f97316"/>
        <text x="30" y="230" textAnchor="middle" fill="#f97316" fontSize="9.5"
          fontFamily="ui-sans-serif,sans-serif"
          transform="rotate(-90,30,230)">Flute L.</text>

        {/* Diameter — horizontal bottom arrow, purple */}
        <line x1="65" y1="320" x2="135" y2="320" stroke="#a78bfa" strokeWidth="1.5"/>
        <polygon points="65,317 65,323 59,320" fill="#a78bfa"/>
        <polygon points="135,317 135,323 141,320" fill="#a78bfa"/>
        <text x="100" y="336" textAnchor="middle" fill="#a78bfa" fontSize="10"
          fontFamily="ui-sans-serif,sans-serif">Diameter (Ø)</text>

        {/* Spindle-to-tip caption */}
        <text x="100" y="360" textAnchor="middle" fill="#4b5563" fontSize="8.5"
          fontFamily="ui-sans-serif,sans-serif">spindle collet → tip</text>
        <text x="100" y="373" textAnchor="middle" fill="#3b82f6" fontSize="8.5"
          fontFamily="ui-sans-serif,sans-serif">= Tool Length</text>
      </svg>

      {/* colour legend */}
      <div className="flex flex-col gap-1 text-[10px] w-full max-w-[170px]">
        <div className="flex items-center gap-1.5">
          <span className="inline-block w-3 h-0.5 bg-blue-500 rounded"/>
          <span className="text-blue-400 font-medium">Tool Length</span>
          <span className="text-gray-600">spindle → tip</span>
        </div>
        <div className="flex items-center gap-1.5">
          <span className="inline-block w-3 h-0.5 bg-orange-500 rounded"/>
          <span className="text-orange-400 font-medium">Flute Length</span>
          <span className="text-gray-600">cutting edge</span>
        </div>
        <div className="flex items-center gap-1.5">
          <span className="inline-block w-3 h-0.5 bg-violet-400 rounded"/>
          <span className="text-violet-400 font-medium">Diameter (Ø)</span>
          <span className="text-gray-600">cutting width</span>
        </div>
      </div>
    </div>
  )
}

// ── Labelled form field with optional tooltip ─────────────────────────────────

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
          <span title={tooltip} className="cursor-help text-gray-600 hover:text-gray-400 text-[10px]">ⓘ</span>
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

  const fluteExceedsLength =
    editing != null &&
    (editing.fluteLengthMm ?? 0) > 0 &&
    (editing.toolLengthMm ?? 0) > 0 &&
    (editing.fluteLengthMm ?? 0) > (editing.toolLengthMm ?? 0)

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <h2 className="text-2xl font-semibold text-white">CNC Tool Library</h2>
        <button
          onClick={() => setEditing({
            type: 'FlatEndMill', fluteCount: 2, toolMaterial: 'HSS',
            recommendedRpm: 10000, recommendedFeedMmPerMin: 500, toolLengthMm: 50,
          })}
          className="px-4 py-2 bg-primary/80 hover:bg-primary text-white text-sm rounded-lg transition"
        >
          + Add Tool
        </button>
      </div>

      <div className="overflow-x-auto">
        <table className="w-full text-sm">
          <thead>
            <tr className="text-gray-500 border-b border-gray-800">
              {['Name', 'Type', 'Ø (mm)', 'Flute L (mm)', 'Tool L (mm)', 'Flutes', 'RPM', 'Feed mm/min', ''].map(h => (
                <th key={h} className="text-left py-2 px-3 font-medium">{h}</th>
              ))}
            </tr>
          </thead>
          <tbody>
            {tools.map(t => (
              <tr key={t.id} className="border-b border-gray-800/50 text-gray-300 hover:bg-gray-900">
                <td className="py-2.5 px-3 font-medium text-white">{t.name}</td>
                <td className="px-3">{t.type}</td>
                <td className="px-3 text-violet-400">{t.diameterMm}</td>
                <td className="px-3 text-orange-400">{t.fluteLengthMm}</td>
                <td className="px-3 text-blue-400">{t.toolLengthMm ?? '—'}</td>
                <td className="px-3">{t.fluteCount}</td>
                <td className="px-3">{t.recommendedRpm.toLocaleString()}</td>
                <td className="px-3">{t.recommendedFeedMmPerMin}</td>
                <td className="px-3">
                  <button onClick={() => deleteMutation.mutate(t.id)} className="text-red-500 hover:text-red-400 text-xs">
                    Remove
                  </button>
                </td>
              </tr>
            ))}
            {tools.length === 0 && (
              <tr>
                <td colSpan={9} className="py-8 text-center text-gray-600 text-sm">
                  No tools yet — click + Add Tool to create one.
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>

      {/* ── Add / Edit modal ── */}
      {editing && (
        <div className="fixed inset-0 bg-black/60 flex items-start justify-center z-50 p-4 overflow-y-auto">
          <div className="bg-gray-900 border border-gray-700 rounded-xl p-6 w-full max-w-3xl space-y-5 my-8">
            <h3 className="font-semibold text-white text-lg">New CNC Tool</h3>

            {/* Two-column layout: diagram (left) + form (right) */}
            <div className="flex gap-6">

              {/* ── Diagram ── */}
              <div className="flex-shrink-0 w-44 bg-gray-950/50 rounded-xl border border-gray-800 p-3 flex flex-col items-center">
                <p className="text-xs text-gray-500 mb-2 text-center">Tool anatomy guide</p>
                <ToolDiagram />
              </div>

              {/* ── Form fields ── */}
              <div className="flex-1 space-y-4">
                {/* Name + Type row */}
                <div className="grid grid-cols-2 gap-3">
                  <FormField label="Tool name">
                    <input className="input w-full" placeholder="e.g. Ø3mm 2-Flute Flat"
                      value={editing.name ?? ''}
                      onChange={e => setEditing({ ...editing, name: e.target.value })} />
                  </FormField>
                  <FormField label="Tool type">
                    <select className="input w-full" value={editing.type}
                      onChange={e => setEditing({ ...editing, type: e.target.value as ToolType })}>
                      {TOOL_TYPES.map(t => <option key={t} value={t}>{t}</option>)}
                    </select>
                  </FormField>
                </div>

                {/* Geometry section */}
                <div>
                  <p className="text-xs text-gray-500 uppercase tracking-wide mb-2">Geometry</p>
                  <div className="grid grid-cols-3 gap-3">
                    <FormField
                      label="Diameter (mm)"
                      labelColor="text-violet-400"
                      tooltip="Cutting diameter of the tool — sets the width of each milling pass and is used for cutter-radius compensation (CRC)."
                    >
                      <input type="number" min={0.1} step={0.1} className="input w-full"
                        value={editing.diameterMm ?? ''}
                        onChange={e => setEditing({ ...editing, diameterMm: +e.target.value })} />
                    </FormField>

                    <FormField
                      label="Flute Length (mm)"
                      labelColor="text-orange-400"
                      tooltip="Length of the cutting edges (flutes). Sets the maximum axial depth of cut. The tool cannot machine deeper than this without the shank rubbing against the part."
                    >
                      <input type="number" min={0.5} step={0.5} className="input w-full"
                        value={editing.fluteLengthMm ?? ''}
                        onChange={e => setEditing({ ...editing, fluteLengthMm: +e.target.value })} />
                    </FormField>

                    <FormField
                      label="Tool Length (mm)"
                      labelColor="text-blue-400"
                      tooltip="Overall length from the spindle collet face to the tool tip. Used for spindle-clearance safety: when the tip is at the cutting depth, the spindle is this far above it. Must be greater than or equal to flute length."
                    >
                      <input type="number" min={1} step={1} className="input w-full"
                        value={editing.toolLengthMm ?? ''}
                        onChange={e => setEditing({ ...editing, toolLengthMm: +e.target.value })} />
                    </FormField>

                    <FormField
                      label="Shank Ø (mm)"
                      tooltip="Diameter of the shank (non-cutting portion held in the collet). Used for pocket-access checks."
                    >
                      <input type="number" min={0} step={0.1} className="input w-full"
                        value={editing.shankDiameterMm ?? ''}
                        onChange={e => setEditing({ ...editing, shankDiameterMm: +e.target.value })} />
                    </FormField>

                    <FormField label="Flute count" tooltip="Number of cutting edges. Affects surface finish and chip load.">
                      <input type="number" min={1} max={12} step={1} className="input w-full"
                        value={editing.fluteCount ?? ''}
                        onChange={e => setEditing({ ...editing, fluteCount: +e.target.value })} />
                    </FormField>

                    <FormField label="Material" tooltip="Tool material (HSS, Carbide, Cobalt…)">
                      <input className="input w-full" placeholder="HSS / Carbide"
                        value={editing.toolMaterial ?? ''}
                        onChange={e => setEditing({ ...editing, toolMaterial: e.target.value })} />
                    </FormField>
                  </div>
                </div>

                {/* Validation warning */}
                {fluteExceedsLength && (
                  <div className="bg-red-950/40 border border-red-700 rounded-lg px-3 py-2 text-xs text-red-400">
                    Flute length ({editing.fluteLengthMm} mm) exceeds tool length ({editing.toolLengthMm} mm).
                    The cutting edges cannot be longer than the overall tool — please correct the values.
                  </div>
                )}

                {/* Cutting parameters section */}
                <div>
                  <p className="text-xs text-gray-500 uppercase tracking-wide mb-2">Cutting parameters</p>
                  <div className="grid grid-cols-3 gap-3">
                    <FormField label="Max DoC (mm)" tooltip="Maximum axial depth of cut per pass. The system warns when the configured machining interval exceeds this value.">
                      <input type="number" min={0} step={0.1} className="input w-full"
                        value={editing.maxDepthOfCutMm ?? ''}
                        onChange={e => setEditing({ ...editing, maxDepthOfCutMm: +e.target.value })} />
                    </FormField>
                    <FormField label="RPM" tooltip="Recommended spindle speed. Used in M3 S{rpm} command.">
                      <input type="number" min={100} step={500} className="input w-full"
                        value={editing.recommendedRpm ?? ''}
                        onChange={e => setEditing({ ...editing, recommendedRpm: +e.target.value })} />
                    </FormField>
                    <FormField label="Feed (mm/min)" tooltip="Recommended XY feed rate during cutting passes.">
                      <input type="number" min={10} step={10} className="input w-full"
                        value={editing.recommendedFeedMmPerMin ?? ''}
                        onChange={e => setEditing({ ...editing, recommendedFeedMmPerMin: +e.target.value })} />
                    </FormField>
                  </div>
                </div>
              </div>
            </div>

            {/* Spindle clearance info box */}
            <div className="bg-blue-950/30 border border-blue-800/50 rounded-lg px-3 py-2 text-xs text-blue-300">
              <span className="font-medium">Spindle clearance: </span>
              when the tool tip is at the machining depth, the spindle collet face is{' '}
              <span className="text-blue-200 font-semibold">
                {(editing.toolLengthMm ?? 50).toFixed(1)} mm above it
              </span>.
              {editing.toolLengthMm && editing.fluteLengthMm && editing.toolLengthMm >= editing.fluteLengthMm && (
                <span className="text-green-400 ml-1">
                  Shank clearance: {((editing.toolLengthMm ?? 0) - (editing.fluteLengthMm ?? 0)).toFixed(1)} mm above flutes.
                </span>
              )}
            </div>

            <div className="flex gap-3 justify-end pt-1">
              <button onClick={() => setEditing(null)}
                className="px-4 py-2 bg-gray-800 text-gray-300 rounded-lg text-sm hover:bg-gray-700 transition">
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
                  className="px-5 py-2 bg-primary/80 hover:bg-primary disabled:opacity-40 text-white rounded-lg text-sm transition"
                >
                  Save Tool
                </button>
              </DisabledHint>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}
