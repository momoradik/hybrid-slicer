import { useState, useEffect, useRef } from 'react'

/**
 * Numeric input that allows clearing the field to empty while typing.
 * Fixes the "02" problem where plain `<input type="number" value={0}>` won't
 * let you delete the last digit. Uses string state internally and commits
 * the parsed number on blur.
 */
export default function NumericInput({ value, min, max, step = 1, onChange, className }: {
  value: number
  min?: number
  max?: number
  step?: number
  onChange: (v: number) => void
  className?: string
}) {
  const [raw, setRaw] = useState(String(value))
  const ref = useRef<HTMLInputElement>(null)

  // Sync when parent value changes (but not while user is editing)
  useEffect(() => {
    if (document.activeElement !== ref.current) setRaw(String(value))
  }, [value])

  return (
    <input
      ref={ref}
      type="number"
      min={min}
      max={max}
      step={step}
      value={raw}
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
          const clamped = Math.max(min ?? -Infinity, Math.min(max ?? Infinity, n))
          setRaw(String(clamped))
          onChange(clamped)
        }
      }}
      className={className ?? 'input w-full'}
    />
  )
}
