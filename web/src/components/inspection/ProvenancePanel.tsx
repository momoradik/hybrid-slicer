/**
 * Provenance display: version fingerprint, algorithm versions, seeds,
 * precision tier, feasibility status.
 * DISPLAY ONLY — all from core.
 */
import type { InspectionResult } from '../../api/alignmesh'

export default function ProvenancePanel({ result }: { result: InspectionResult }) {
  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl p-5">
      <h3 className="text-sm font-medium text-gray-300 uppercase tracking-wider mb-3">Provenance & Traceability</h3>

      <div className="grid grid-cols-1 md:grid-cols-2 gap-4 text-xs">
        {/* Software */}
        <div className="space-y-1.5">
          <p className="text-gray-500 uppercase font-medium">Software</p>
          <div className="bg-gray-800 rounded-lg p-2 space-y-1">
            <p><span className="text-gray-500">Core: </span><span className="text-gray-200 font-mono">{result.core_version}</span></p>
            <p><span className="text-gray-500">Compiler: </span><span className="text-gray-200 font-mono">{result.fingerprint.compiler}</span></p>
            <p><span className="text-gray-500">CPU: </span><span className="text-gray-200 font-mono">{result.fingerprint.cpu}</span></p>
          </div>
        </div>

        {/* Input data */}
        <div className="space-y-1.5">
          <p className="text-gray-500 uppercase font-medium">Input Data</p>
          <div className="bg-gray-800 rounded-lg p-2 space-y-1">
            <p><span className="text-gray-500">Ref SHA-256: </span><span className="text-gray-200 font-mono text-[10px] break-all">{result.reference_hash}</span></p>
            <p><span className="text-gray-500">Meas SHA-256: </span><span className="text-gray-200 font-mono text-[10px] break-all">{result.measured_hash}</span></p>
            <p><span className="text-gray-500">Timestamp: </span><span className="text-gray-200">{result.timestamp}</span></p>
          </div>
        </div>

        {/* Assessment */}
        <div className="space-y-1.5">
          <p className="text-gray-500 uppercase font-medium">Assessment</p>
          <div className="bg-gray-800 rounded-lg p-2 space-y-1">
            <p><span className="text-gray-500">Precision tier: </span><span className="text-gray-200">{result.precision_tier}</span></p>
            <p><span className="text-gray-500">Alignment: </span><span className="text-gray-200">{result.alignment_mode}</span></p>
            <p><span className="text-gray-500">Tolerance: </span><span className="text-gray-200 font-mono">{result.tolerance_mm + ' mm'}</span></p>
          </div>
        </div>

        {/* Transform */}
        <div className="space-y-1.5">
          <p className="text-gray-500 uppercase font-medium">Alignment Transform</p>
          <div className="bg-gray-800 rounded-lg p-2">
            {result.transform_matrix && result.transform_matrix.length === 16 ? (
              <div className="font-mono text-[9px] text-gray-300 leading-relaxed">
                {[0, 1, 2, 3].map(row => (
                  <p key={row}>
                    {'[' + result.transform_matrix.slice(row * 4, row * 4 + 4).map(v => v.toFixed(6)).join('  ') + ']'}
                  </p>
                ))}
              </div>
            ) : (
              <p className="text-gray-500">Not available</p>
            )}
          </div>
        </div>
      </div>

      {/* Heatmap caveat */}
      <p className="text-[10px] text-yellow-600 mt-3 italic">{result.heatmap_label}</p>
    </div>
  )
}
