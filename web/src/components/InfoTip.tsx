/**
 * Hover explanation for a control or label.
 *
 * Two shapes:
 *   <InfoTip text="What this does" />              — a small ⓘ badge you place
 *                                                     next to a label.
 *   <InfoTip text="…">{children}</InfoTip>         — wraps arbitrary content and
 *                                                     explains it on hover.
 *
 * Same CSS-only group-hover approach as DisabledHint, so it also works over
 * disabled inputs (which swallow pointer events). Focus shows the tip too, so
 * it is reachable by keyboard.
 */
export default function InfoTip({
  text,
  cura,
  side = 'top',
  children,
}: {
  /** Plain-language explanation. Required — an InfoTip with nothing to say is noise. */
  text: string
  /** Optional CuraEngine setting key, rendered in mono as the second line. */
  cura?: string
  side?: 'top' | 'bottom'
  children?: React.ReactNode
}) {
  const position =
    side === 'top'
      ? 'bottom-full mb-2'
      : 'top-full mt-2'

  const arrow =
    side === 'top'
      ? 'top-full -mt-px border-t-gray-600'
      : 'bottom-full -mb-px border-b-gray-600'

  return (
    <span className="relative group inline-flex items-center align-middle">
      {children ?? (
        <span
          tabIndex={0}
          aria-label={text}
          className="
            inline-flex items-center justify-center w-3.5 h-3.5 rounded-full
            border border-gray-600 text-gray-500 text-[9px] font-semibold leading-none
            cursor-help select-none hover:border-gray-400 hover:text-gray-300
            focus:outline-none focus:border-primary focus:text-primary transition-colors
          "
        >
          i
        </span>
      )}
      <span
        role="tooltip"
        className={`
          pointer-events-none absolute z-50 left-1/2 -translate-x-1/2 ${position}
          px-3 py-2 rounded-lg text-xs leading-snug text-gray-200
          bg-gray-800 border border-gray-600 shadow-lg
          w-64 max-w-[16rem] whitespace-normal text-left
          opacity-0 group-hover:opacity-100 group-focus-within:opacity-100
          transition-opacity duration-150
        `}
      >
        {text}
        {cura && (
          <span className="block mt-1 font-mono text-[10px] text-gray-400">{cura}</span>
        )}
        <span
          className={`absolute left-1/2 -translate-x-1/2 ${arrow} border-4 border-transparent`}
        />
      </span>
    </span>
  )
}
