/**
 * Styled toggle switch replacing plain checkboxes.
 * Works as a controlled component with checked/onChange.
 */
export default function Toggle({ checked, onChange, label, sublabel }: {
  checked: boolean
  onChange: (v: boolean) => void
  label?: string
  sublabel?: string
}) {
  return (
    <label className="flex items-center gap-2.5 cursor-pointer select-none group">
      <button
        type="button"
        role="switch"
        aria-checked={checked}
        onClick={() => onChange(!checked)}
        className={`relative w-9 h-5 rounded-full transition-colors flex-shrink-0 ${
          checked ? 'bg-primary' : 'bg-gray-600 group-hover:bg-gray-500'
        }`}
      >
        <span className={`absolute top-0.5 left-0.5 w-4 h-4 rounded-full bg-white transition-transform shadow-sm ${
          checked ? 'translate-x-4' : ''
        }`} />
      </button>
      {(label || sublabel) && (
        <span className="flex flex-col">
          {label && <span className="text-sm text-gray-300">{label}</span>}
          {sublabel && <span className="text-[10px] text-gray-500 leading-tight">{sublabel}</span>}
        </span>
      )}
    </label>
  )
}
