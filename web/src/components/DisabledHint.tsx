/**
 * Wraps a potentially disabled control and shows a tooltip on hover explaining
 * why it is disabled and what the user needs to do.
 *
 * Usage:
 *   <DisabledHint when={!jobId} reason="Select a job first.">
 *     <button disabled={!jobId} ...>Go</button>
 *   </DisabledHint>
 *
 * When `when` is false the wrapper is transparent — no extra DOM, no tooltip.
 * The tooltip uses a CSS-only approach (group-hover) so it works even over
 * natively disabled <button>/<select> elements (which swallow pointer events).
 */
export default function DisabledHint({
  when,
  reason,
  children,
}: {
  when: boolean
  reason: string
  children: React.ReactNode
}) {
  if (!when) return <>{children}</>

  return (
    <div className="relative group">
      {/* Capture pointer events even when the child is disabled */}
      <div className="pointer-events-auto">{children}</div>
      <div
        role="tooltip"
        className="
          pointer-events-none absolute z-50 bottom-full left-1/2 -translate-x-1/2 mb-2
          px-3 py-1.5 rounded-lg text-xs leading-snug text-gray-200 bg-gray-800 border border-gray-600
          whitespace-normal max-w-sm text-center shadow-lg
          opacity-0 group-hover:opacity-100 transition-opacity duration-200
        "
      >
        {reason}
        <div className="absolute top-full left-1/2 -translate-x-1/2 -mt-px border-4 border-transparent border-t-gray-600" />
      </div>
    </div>
  )
}
