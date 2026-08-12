import { useState, useMemo, useEffect } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { customGCodeApi, machineProfilesApi } from '../api/client'
import InfoTip from '../components/InfoTip'
import DisabledHint from '../components/DisabledHint'
import type { CustomGCodeBlock, GCodeTrigger, MachineProfile, ApiError } from '../types'

/**
 * G-code Customisation.
 *
 * The model, stated once so the page doesn't have to be guessed at:
 *   • Every block belongs to ONE machine, or is Shared (applies to all machines).
 *   • You pick a machine at the top; the page then shows exactly the blocks that
 *     will run on that machine — its own plus the shared ones.
 *   • Each block has a trigger that says WHEN it runs. Blocks are listed in the
 *     order they actually fire during a job.
 *   • The checkbox activates/deactivates a block for that machine.
 */

type TriggerMeta = {
  value: GCodeTrigger
  label: string
  /** When this fires, in plain language. */
  hint: string
}

// Ordered by when they fire during a job — this ordering drives the page layout.
const BASE_TRIGGERS: TriggerMeta[] = [
  { value: 'JobStart',        label: 'Job Start',       hint: 'Runs once, at the very top of the file — before anything is printed. Replaces Cura\'s default header (temperatures, homing).' },
  { value: 'EveryNLayers',    label: 'Every N Layers',  hint: 'Runs on a layer cadence — e.g. every 10 layers. Fires after the layer finishes printing. Use for periodic purges, wipes, probes or pauses.' },
  { value: 'BeforePrinting',  label: 'Before Printing', hint: 'Runs immediately before each printing phase resumes.' },
  { value: 'AfterPrinting',   label: 'After Printing',  hint: 'Runs immediately after each printing phase ends.' },
  { value: 'BeforeMachining', label: 'Before Machining', hint: 'Runs before each CNC phase — typically spindle spin-up, tool change or dust extraction on.' },
  { value: 'AfterMachining',  label: 'After Machining',  hint: 'Runs after each CNC phase — typically spindle stop, park or chip clearing.' },
  { value: 'JobEnd',          label: 'Job End',          hint: 'Runs once, at the very bottom of the file. Replaces Cura\'s default footer.' },
]

function buildExtruderTriggers(extruderCount: number): TriggerMeta[] {
  const result: TriggerMeta[] = []
  for (let i = 0; i < Math.min(extruderCount, 8); i++) {
    result.push({
      value: `BeforeExtruder${i}` as GCodeTrigger,
      label: `Before Extruder ${i + 1}`,
      hint: `Runs just before the machine switches to extruder ${i + 1}.`,
    })
    result.push({
      value: `AfterExtruder${i}` as GCodeTrigger,
      label: `After Extruder ${i + 1}`,
      hint: `Runs just after extruder ${i + 1} finishes its section.`,
    })
  }
  return result
}

type Draft = Partial<CustomGCodeBlock> & { trigger: GCodeTrigger }

const NEW_BLOCK = (machineId: string): Draft => ({
  name: '',
  gCodeContent: '',
  trigger: 'JobStart',
  isEnabled: true,
  sortOrder: 0,
  machineProfileId: machineId || null,
  repeatEveryNLayers: 10,
  // Phase the cadence at the interval so the default reads the way people say it:
  // "every 10 layers" fires on 10, 20, 30 — not 1, 11, 21.
  startLayer: 10,
  endLayer: null,
})

export default function CustomGCode() {
  const qc = useQueryClient()
  const { data: machines = [] } = useQuery({ queryKey: ['machines'], queryFn: machineProfilesApi.getAll })
  const [machineId, setMachineId] = useState('')

  // Default to the first machine so the page is never in a meaningless "no machine" state.
  useEffect(() => {
    if (!machineId && machines.length > 0) setMachineId(machines[0].id)
  }, [machines, machineId])

  const { data: blocks = [], isLoading } = useQuery({
    queryKey: ['gcode-blocks', machineId],
    queryFn: () => customGCodeApi.getAll(machineId || undefined),
    enabled: !!machineId,
  })

  const [editing, setEditing] = useState<Draft | null>(null)
  const [error, setError] = useState<ApiError | null>(null)
  const [toast, setToast] = useState<string | null>(null)

  const machine: MachineProfile | undefined = machines.find(m => m.id === machineId)

  const allTriggers = useMemo(
    () => [...BASE_TRIGGERS, ...buildExtruderTriggers(machine?.extruderCount ?? 1)],
    [machine?.extruderCount],
  )

  const showToast = (msg: string) => { setToast(msg); setTimeout(() => setToast(null), 2500) }
  const invalidate = () => qc.invalidateQueries({ queryKey: ['gcode-blocks'] })

  // The API returns a {code,message,field,providedValue,expected} envelope — surface
  // the real reason rather than a generic failure.
  const onError = (e: any) => setError(
    e?.response?.data ?? { code: 'NETWORK', message: e?.message ?? 'Request failed.' })

  const createMutation = useMutation({
    mutationFn: customGCodeApi.create,
    onSuccess: () => { invalidate(); setEditing(null); setError(null); showToast('Block created') },
    onError,
  })

  const updateMutation = useMutation({
    mutationFn: ({ id, ...data }: Draft & { id: string }) => customGCodeApi.update(id, data),
    onSuccess: () => { invalidate(); setEditing(null); setError(null); showToast('Block saved') },
    onError,
  })

  const toggleMutation = useMutation({
    mutationFn: ({ id, enabled }: { id: string; enabled: boolean }) => customGCodeApi.toggle(id, enabled),
    onSuccess: invalidate,
    onError,
  })

  const deleteMutation = useMutation({
    mutationFn: customGCodeApi.delete,
    onSuccess: () => { invalidate(); showToast('Block deleted') },
    onError,
  })

  const handleSave = () => {
    if (!editing) return
    setError(null)
    const payload: Draft = {
      ...editing,
      // The cadence fields are meaningless off the EveryNLayers trigger; send
      // canonical values so the server never stores stale interval data.
      repeatEveryNLayers: editing.trigger === 'EveryNLayers' ? (editing.repeatEveryNLayers ?? 0) : 0,
      startLayer: editing.startLayer ?? 1,
      endLayer: editing.endLayer ?? null,
    }
    if (payload.id) updateMutation.mutate(payload as Draft & { id: string })
    else createMutation.mutate(payload)
  }

  // Group blocks by trigger, keeping the execution-order sequence of allTriggers.
  const grouped = useMemo(() => {
    return allTriggers
      .map(meta => ({ meta, items: blocks.filter(b => b.trigger === meta.value) }))
      .filter(g => g.items.length > 0)
  }, [blocks, allTriggers])

  const sharedCount = blocks.filter(b => !b.machineProfileId).length
  const ownCount = blocks.length - sharedCount
  const activeCount = blocks.filter(b => b.isEnabled).length

  return (
    <div className="space-y-6">
      <div className="flex items-start justify-between gap-4">
        <div>
          <h2 className="text-2xl font-semibold text-white">G-code Customisation</h2>
          <p className="text-sm text-gray-400 mt-1 max-w-3xl">
            Snippets of your own G-code, injected into generated jobs at a chosen moment.
            Blocks belong to a machine — pick the machine below to see exactly what will run on it.
          </p>
        </div>
        <DisabledHint when={!machineId} reason="Create a machine in Machine Configuration first.">
          <button
            onClick={() => { setError(null); setEditing(NEW_BLOCK(machineId)) }}
            disabled={!machineId}
            className="px-4 py-2 bg-primary/80 hover:bg-primary disabled:opacity-40 text-white text-sm rounded-lg shrink-0"
          >
            + New Block
          </button>
        </DisabledHint>
      </div>

      {/* ── Machine selector: the control everything else depends on ── */}
      <div className="bg-gray-900 border border-gray-800 rounded-xl p-5">
        <div className="flex flex-wrap items-end gap-6">
          <div className="space-y-1.5">
            <label className="flex items-center gap-1.5 text-xs font-medium text-gray-300">
              Machine
              <InfoTip text="G-code customisation is per machine. Each machine has its own set of blocks; switching machines here shows that machine's blocks, which you can activate or deactivate independently." />
            </label>
            <select
              className="input min-w-[16rem]"
              value={machineId}
              onChange={e => setMachineId(e.target.value)}
            >
              {machines.length === 0 && <option value="">No machines configured</option>}
              {machines.map(m => (
                <option key={m.id} value={m.id}>
                  {m.name} — {m.extruderCount} extruder{m.extruderCount > 1 ? 's' : ''}
                </option>
              ))}
            </select>
          </div>

          {machineId && (
            <div className="flex items-center gap-5 text-xs text-gray-400 pb-2">
              <Stat label="Active" value={activeCount} tip="Blocks that are switched on and will be injected into this machine's jobs." />
              <Stat label="This machine" value={ownCount} tip="Blocks that belong only to the selected machine." />
              <Stat label="Shared" value={sharedCount} tip="Blocks with no machine set — they run on every machine." />
            </div>
          )}
        </div>
      </div>

      {/* ── How it works ── */}
      {machineId && (
        <div className="bg-gray-900/50 border border-gray-800 rounded-xl p-4 text-xs text-gray-400 space-y-1.5">
          <p className="text-gray-300 font-medium">What goes where</p>
          <p>
            <span className="text-gray-200">Trigger</span> decides <em>when</em> a block runs — the sections
            below are listed in the order they actually fire during a job.
          </p>
          <p>
            <span className="text-gray-200">Scope</span> decides <em>where</em> it runs — “This machine” only on{' '}
            {machine?.name}, “Shared” on every machine.
          </p>
          <p>
            <span className="text-gray-200">The checkbox</span> turns a block on or off without deleting it.
          </p>
          <p>
            To skip every block for one particular job, untick “Apply custom G-code blocks” on the STL Import screen.
          </p>
        </div>
      )}

      {/* ── Blocks, grouped by trigger in execution order ── */}
      {isLoading && <p className="text-gray-500 text-sm">Loading…</p>}

      {machineId && !isLoading && blocks.length === 0 && (
        <div className="border border-dashed border-gray-800 rounded-xl py-12 text-center">
          <p className="text-gray-500 text-sm">No G-code blocks for {machine?.name} yet.</p>
          <p className="text-gray-600 text-xs mt-1">Click “+ New Block” to add one.</p>
        </div>
      )}

      <div className="space-y-6">
        {grouped.map(({ meta, items }) => (
          <section key={meta.value} className="space-y-2">
            <div className="flex items-center gap-2">
              <h3 className="text-sm font-semibold text-white">{meta.label}</h3>
              <InfoTip text={meta.hint} />
              <span className="text-[10px] text-gray-600">{items.length} block{items.length > 1 ? 's' : ''}</span>
            </div>
            <p className="text-xs text-gray-500 -mt-1">{meta.hint}</p>

            <div className="grid gap-2">
              {items.map(block => (
                <BlockRow
                  key={block.id}
                  block={block}
                  machineName={machine?.name}
                  onToggle={enabled => toggleMutation.mutate({ id: block.id, enabled })}
                  onEdit={() => { setError(null); setEditing({ ...block, trigger: block.trigger }) }}
                  onDelete={() => deleteMutation.mutate(block.id)}
                />
              ))}
            </div>
          </section>
        ))}
      </div>

      {/* ── Editor ── */}
      {editing && (
        <Editor
          draft={editing}
          setDraft={setEditing}
          triggers={allTriggers}
          machineName={machine?.name ?? 'this machine'}
          machineId={machineId}
          error={error}
          saving={createMutation.isPending || updateMutation.isPending}
          onCancel={() => { setEditing(null); setError(null) }}
          onSave={handleSave}
        />
      )}

      {toast && (
        <div className="fixed bottom-6 right-6 px-4 py-2 rounded-lg bg-green-900/80 border border-green-700 text-green-200 text-sm shadow-lg">
          {toast}
        </div>
      )}
    </div>
  )
}

// ── Pieces ──────────────────────────────────────────────────────────────────

function Stat({ label, value, tip }: { label: string; value: number; tip: string }) {
  return (
    <span className="flex items-center gap-1.5">
      <span className="text-white font-semibold">{value}</span>
      <span>{label}</span>
      <InfoTip text={tip} />
    </span>
  )
}

function BlockRow({
  block, machineName, onToggle, onEdit, onDelete,
}: {
  block: CustomGCodeBlock
  machineName?: string
  onToggle: (enabled: boolean) => void
  onEdit: () => void
  onDelete: () => void
}) {
  const shared = !block.machineProfileId

  return (
    <div className={`bg-gray-900 border rounded-xl p-4 flex items-start gap-4 transition-opacity ${
      block.isEnabled ? 'border-gray-800' : 'border-gray-850 opacity-50'
    }`}>
      <InfoTip text={block.isEnabled
        ? 'Active — this block is injected into generated G-code. Untick to disable it without deleting.'
        : 'Inactive — kept, but not injected. Tick to enable it again.'}>
        <input
          type="checkbox"
          checked={block.isEnabled}
          onChange={e => onToggle(e.target.checked)}
          className="w-4 h-4 accent-primary mt-0.5 cursor-pointer"
        />
      </InfoTip>

      <div className="flex-1 min-w-0">
        <div className="flex items-center gap-2 flex-wrap">
          <p className="font-medium text-white">{block.name}</p>

          <InfoTip text={shared
            ? 'Shared block — runs on every machine.'
            : `Belongs to ${machineName ?? 'this machine'} only.`}>
            <span className={`px-2 py-0.5 text-xs rounded-full ${
              shared ? 'bg-violet-900/40 text-violet-300' : 'bg-gray-800 text-gray-400'
            }`}>
              {shared ? 'Shared' : 'This machine'}
            </span>
          </InfoTip>

          {block.trigger === 'EveryNLayers' && (
            <InfoTip text={`Fires every ${block.repeatEveryNLayers} layers, starting at layer ${block.startLayer}${
              block.endLayer ? `, stopping after layer ${block.endLayer}` : ' and continuing to the end of the print'
            }.`}>
              <span className="px-2 py-0.5 bg-amber-900/40 text-amber-300 text-xs rounded-full">
                every {block.repeatEveryNLayers} layers
                {block.startLayer > 1 && ` · from ${block.startLayer}`}
                {block.endLayer ? ` · to ${block.endLayer}` : ''}
              </span>
            </InfoTip>
          )}
        </div>

        {block.description && (
          <p className="text-xs text-gray-500 mt-1">{block.description}</p>
        )}

        <pre className="mt-2 text-xs text-gray-500 font-mono bg-gray-950 rounded p-2 overflow-x-auto max-h-24">
          {block.gCodeContent}
        </pre>
      </div>

      <div className="flex gap-3 shrink-0">
        <button onClick={onEdit} className="text-gray-400 hover:text-white text-xs">Edit</button>
        <button onClick={onDelete} className="text-red-500 hover:text-red-400 text-xs">Delete</button>
      </div>
    </div>
  )
}

function Editor({
  draft, setDraft, triggers, machineName, machineId, error, saving, onCancel, onSave,
}: {
  draft: Draft
  setDraft: (d: Draft) => void
  triggers: TriggerMeta[]
  machineName: string
  machineId: string
  error: ApiError | null
  saving: boolean
  onCancel: () => void
  onSave: () => void
}) {
  const set = <K extends keyof Draft>(k: K, v: Draft[K]) => setDraft({ ...draft, [k]: v })
  const meta = triggers.find(t => t.value === draft.trigger)
  const isPeriodic = draft.trigger === 'EveryNLayers'

  // Highlight the exact input the server rejected.
  const fieldError = (name: string) => error?.field === name

  return (
    <div className="fixed inset-0 bg-black/60 flex items-center justify-center z-50 p-4">
      <div className="bg-gray-900 border border-gray-700 rounded-xl w-full max-w-2xl max-h-[92vh] overflow-y-auto">
        <div className="p-6 space-y-5">
          <h3 className="font-semibold text-white text-lg">
            {draft.id ? 'Edit' : 'New'} G-code Block
          </h3>

          {error && (
            <div className="bg-red-950/50 border border-red-800 rounded-lg p-3 space-y-1">
              <p className="text-sm text-red-300 font-medium">Couldn’t save this block</p>
              <p className="text-xs text-red-300/90">{error.message}</p>
              {error.field && (
                <p className="text-[11px] text-red-400/80">
                  Field: <span className="font-mono">{error.field}</span>
                  {error.providedValue && <> · you entered <span className="font-mono">{error.providedValue}</span></>}
                </p>
              )}
            </div>
          )}

          <Field label="Block name" tip="A short name you'll recognise in the list, e.g. “Purge and wipe”.">
            <input
              className={`input w-full ${fieldError('name') ? 'border-red-600' : ''}`}
              placeholder="e.g. Purge and wipe"
              value={draft.name ?? ''}
              onChange={e => set('name', e.target.value)}
            />
          </Field>

          <div className="grid grid-cols-2 gap-4">
            <Field label="Scope" tip="Whether this block belongs to the selected machine only, or runs on every machine.">
              <select
                className="input w-full"
                value={draft.machineProfileId ? 'machine' : 'shared'}
                onChange={e => set('machineProfileId', e.target.value === 'machine' ? machineId : null)}
              >
                <option value="machine">This machine — {machineName}</option>
                <option value="shared">Shared — every machine</option>
              </select>
            </Field>

            <Field label="Trigger — when it runs" tip={meta?.hint ?? 'Chooses the moment this block is injected.'}>
              <select
                className="input w-full"
                value={draft.trigger}
                onChange={e => set('trigger', e.target.value as GCodeTrigger)}
              >
                {triggers.map(t => <option key={t.value} value={t.value}>{t.label}</option>)}
              </select>
            </Field>
          </div>

          {meta && (
            <p className="text-xs text-gray-500 bg-gray-950 border border-gray-800 rounded-lg p-3 -mt-1">
              {meta.hint}
            </p>
          )}

          {/* Cadence — only meaningful for the layer-interval trigger */}
          {isPeriodic && (
            <div className="pl-3 border-l-2 border-amber-800/60 space-y-3">
              <p className="text-xs text-amber-300/90">
                Layer cadence — the block is injected once the listed layer has finished printing.
              </p>
              <CadencePreview
                every={draft.repeatEveryNLayers ?? 0}
                start={draft.startLayer ?? 1}
                end={draft.endLayer ?? null}
              />
              <div className="grid grid-cols-3 gap-4">
                <Field label="Every N layers" tip="The interval. 10 means the block runs once every 10 layers." required>
                  <input
                    type="number" min={1}
                    className={`input w-full ${fieldError('repeatEveryNLayers') ? 'border-red-600' : ''}`}
                    value={draft.repeatEveryNLayers ?? 10}
                    onChange={e => set('repeatEveryNLayers', Number(e.target.value))}
                  />
                </Field>
                <Field label="Start layer" tip="First layer the cadence may fire on. Layers are numbered from 1.">
                  <input
                    type="number" min={1}
                    className={`input w-full ${fieldError('startLayer') ? 'border-red-600' : ''}`}
                    value={draft.startLayer ?? 1}
                    onChange={e => set('startLayer', Number(e.target.value))}
                  />
                </Field>
                <Field label="End layer" tip="Last layer the cadence may fire on. Leave empty to continue to the end of the print.">
                  <input
                    type="number" min={1}
                    placeholder="end of print"
                    className={`input w-full ${fieldError('endLayer') ? 'border-red-600' : ''}`}
                    value={draft.endLayer ?? ''}
                    onChange={e => set('endLayer', e.target.value === '' ? null : Number(e.target.value))}
                  />
                </Field>
              </div>
            </div>
          )}

          <Field label="G-code" tip="The lines injected verbatim at the trigger point. Comments start with a semicolon." required>
            <textarea
              className={`input w-full font-mono text-xs h-40 resize-none ${fieldError('gCodeContent') ? 'border-red-600' : ''}`}
              placeholder={'; e.g.\nG1 E10 F300 ; purge\nG1 X0 Y0 F3000 ; wipe'}
              value={draft.gCodeContent ?? ''}
              onChange={e => set('gCodeContent', e.target.value)}
            />
          </Field>

          <Field label="Description" tip="Optional note to remind you what this block is for.">
            <input
              className="input w-full"
              placeholder="Optional"
              value={draft.description ?? ''}
              onChange={e => set('description', e.target.value)}
            />
          </Field>

          <div className="flex gap-3 justify-end pt-2">
            <button onClick={onCancel} className="px-4 py-2 bg-gray-800 hover:bg-gray-700 text-gray-300 rounded-lg text-sm">
              Cancel
            </button>
            <button
              onClick={onSave}
              disabled={saving}
              className="px-4 py-2 bg-primary/80 hover:bg-primary disabled:opacity-40 text-white rounded-lg text-sm"
            >
              {saving ? 'Saving…' : 'Save Block'}
            </button>
          </div>
        </div>
      </div>
    </div>
  )
}

/**
 * Spells out the layers a cadence actually hits, so "every 10 layers" is never
 * ambiguous about whether it starts at 1 or at 10.
 */
function CadencePreview({ every, start, end }: { every: number; start: number; end: number | null }) {
  if (!Number.isFinite(every) || every < 1 || !Number.isFinite(start) || start < 1) {
    return (
      <p className="text-xs text-red-400/90 bg-red-950/30 border border-red-900/50 rounded-lg p-2.5">
        Enter an interval of 1 or more to see which layers this will fire on.
      </p>
    )
  }
  if (end !== null && end < start) {
    return (
      <p className="text-xs text-red-400/90 bg-red-950/30 border border-red-900/50 rounded-lg p-2.5">
        End layer ({end}) is before the start layer ({start}) — this block would never fire.
      </p>
    )
  }

  const shown: number[] = []
  for (let l = start; shown.length < 6; l += every) {
    if (end !== null && l > end) break
    shown.push(l)
  }
  const more = end === null || start + shown.length * every <= end

  return (
    <div className="text-xs bg-gray-950 border border-gray-800 rounded-lg p-2.5">
      <span className="text-gray-400">Fires after layer{shown.length > 1 ? 's' : ''}: </span>
      <span className="font-mono text-amber-300">
        {shown.length === 0 ? '(never — check the range)' : shown.join(', ')}
        {shown.length > 0 && more ? ', …' : ''}
      </span>
      {end !== null && <span className="text-gray-500"> (stops after layer {end})</span>}
    </div>
  )
}

function Field({
  label, tip, required, children,
}: {
  label: string
  tip: string
  required?: boolean
  children: React.ReactNode
}) {
  return (
    <div className="space-y-1.5">
      <label className="flex items-center gap-1.5 text-xs font-medium text-gray-300">
        {label}
        {required && <span className="text-red-400">*</span>}
        <InfoTip text={tip} />
      </label>
      {children}
    </div>
  )
}
