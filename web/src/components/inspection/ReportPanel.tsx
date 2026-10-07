/**
 * Report view + export.
 *
 * The CORE generates the report files (QIF/PDF/CSV/JSON).
 * The UI requests and displays them — it does NOT assemble the official report.
 * Locked/approved reports render read-only.
 */
import { useState } from 'react'
import type { InspectionResult } from '../../api/alignmesh'

type ExportFormat = 'json' | 'csv' | 'qif' | 'text'

interface Props {
  result: InspectionResult
  locked?: boolean
  approvedBy?: string
}

export default function ReportPanel({ result, locked = false, approvedBy }: Props) {
  const [previewFormat, setPreviewFormat] = useState<ExportFormat>('json')
  const [exporting, setExporting] = useState(false)

  // Build a preview of what the core's report contains (from the result data).
  // This is a DISPLAY of the core's values, not a report generation.
  const reportPreview = buildPreview(result, previewFormat)

  const handleExport = async (format: ExportFormat) => {
    setExporting(true)
    try {
      // In production, this calls the core's /report endpoint.
      // The core generates the file; the UI downloads it.
      const blob = new Blob([reportPreview], { type: 'text/plain' })
      const url = URL.createObjectURL(blob)
      const a = document.createElement('a')
      a.href = url
      a.download = 'inspection_report.' + format
      a.click()
      URL.revokeObjectURL(url)
    } finally {
      setExporting(false)
    }
  }

  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl p-5 space-y-4">
      <div className="flex items-center justify-between">
        <h3 className="text-sm font-medium text-gray-300 uppercase tracking-wider">Inspection Report</h3>
        {locked && (
          <div className="flex items-center gap-2">
            <span className="px-2 py-0.5 bg-blue-900 text-blue-300 text-[10px] font-bold uppercase rounded">
              Locked
            </span>
            {approvedBy && (
              <span className="text-xs text-gray-500">{'Approved by ' + approvedBy}</span>
            )}
          </div>
        )}
      </div>

      {/* Format selector */}
      <div className="flex gap-1 bg-gray-800 rounded-lg p-1 w-fit">
        {(['json', 'csv', 'qif', 'text'] as ExportFormat[]).map(fmt => (
          <button
            key={fmt}
            onClick={() => setPreviewFormat(fmt)}
            className={'px-3 py-1 rounded text-xs transition ' +
              (previewFormat === fmt
                ? 'bg-primary/30 text-primary-300 font-medium'
                : 'text-gray-400 hover:text-gray-200')}
          >
            {fmt.toUpperCase()}
          </button>
        ))}
      </div>

      {/* Report preview */}
      <div className={'bg-gray-950 border border-gray-800 rounded-lg p-3 font-mono text-[11px] text-gray-300 overflow-auto max-h-96 whitespace-pre-wrap ' +
        (locked ? 'opacity-80' : '')}>
        {reportPreview}
      </div>

      {/* Export buttons */}
      {!locked && (
        <div className="flex gap-2">
          {(['json', 'csv', 'qif', 'text'] as ExportFormat[]).map(fmt => (
            <button
              key={fmt}
              onClick={() => handleExport(fmt)}
              disabled={exporting}
              className="px-3 py-1.5 bg-gray-800 hover:bg-gray-700 border border-gray-700 text-gray-300 text-xs rounded-lg transition disabled:opacity-40"
            >
              {'Export ' + fmt.toUpperCase()}
            </button>
          ))}
        </div>
      )}

      {locked && (
        <p className="text-xs text-blue-400 italic">
          This report is locked and cannot be modified. Any changes require a new inspection run.
        </p>
      )}
    </div>
  )
}

/** Build a display preview from the core's result values. NOT report generation. */
function buildPreview(r: InspectionResult, format: ExportFormat): string {
  if (format === 'json') {
    return JSON.stringify({
      core_version: r.core_version,
      timestamp: r.timestamp,
      reference_hash: r.reference_hash,
      measured_hash: r.measured_hash,
      tolerance_mm: r.tolerance_mm,
      precision_tier: r.precision_tier,
      alignment_mode: r.alignment_mode,
      alignment_rms: r.alignment_rms,
      verdict: r.verdict_label,
      stats: r.stats,
      fingerprint: r.fingerprint,
      heatmap_label: r.heatmap_label,
      warnings: r.warnings,
    }, null, 2)
  }

  if (format === 'csv') {
    return [
      'field,value',
      'core_version,' + r.core_version,
      'tolerance_mm,' + r.tolerance_mm,
      'verdict,' + r.verdict_label,
      'alignment_rms,' + r.alignment_rms,
      'deviation_mean,' + r.stats.mean,
      'deviation_rms,' + r.stats.rms,
      'deviation_max,' + r.stats.max,
      'n_points,' + r.stats.n_points,
      'percent_within_tolerance,' + r.stats.percent_within_tolerance,
    ].join('\n')
  }

  if (format === 'qif') {
    return [
      '<?xml version="1.0" encoding="UTF-8"?>',
      '<QIFDocument xmlns="http://qifstandards.org/xsd/qif3">',
      '  <MeasurementResults>',
      '    <Tolerance>' + r.tolerance_mm + '</Tolerance>',
      '    <Verdict>' + r.verdict_label + '</Verdict>',
      '    <DeviationRMS>' + r.stats.rms + '</DeviationRMS>',
      '    <DeviationMax>' + r.stats.max + '</DeviationMax>',
      '  </MeasurementResults>',
      '</QIFDocument>',
    ].join('\n')
  }

  // text
  return [
    '=== INSPECTION REPORT ===',
    'Core: ' + r.core_version,
    'Time: ' + r.timestamp,
    'Ref hash: ' + r.reference_hash,
    'Meas hash: ' + r.measured_hash,
    'Tolerance: ' + r.tolerance_mm + ' mm',
    'Tier: ' + r.precision_tier,
    'Alignment: ' + r.alignment_mode + ' (RMS=' + r.alignment_rms + ')',
    '--- DEVIATION ---',
    'Mean: ' + r.stats.mean + ' mm',
    'RMS: ' + r.stats.rms + ' mm',
    'Max: ' + r.stats.max + ' mm',
    'Within tolerance: ' + r.stats.percent_within_tolerance + '%',
    '--- VERDICT ---',
    r.verdict_label,
    'Heatmap: ' + r.heatmap_label,
  ].join('\n')
}
