import { useState } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { Link } from 'react-router-dom'
import { jobsApi } from '../api/client'
import type { JobStatus, PrintJob } from '../types'

const STATUS_COLOR: Record<JobStatus, string> = {
  Draft:               'bg-gray-700 text-gray-300',
  StlImported:         'bg-blue-900 text-blue-300',
  Slicing:             'bg-yellow-900 text-yellow-300 animate-pulse',
  SlicingComplete:     'bg-blue-800 text-blue-200',
  GeneratingToolpaths: 'bg-purple-900 text-purple-300 animate-pulse',
  ToolpathsComplete:   'bg-purple-800 text-purple-200',
  PlanningHybrid:      'bg-orange-900 text-orange-300 animate-pulse',
  Ready:               'bg-green-900 text-green-300',
  Running:             'bg-green-700 text-green-100 animate-pulse',
  Paused:              'bg-yellow-800 text-yellow-200',
  Complete:            'bg-green-950 text-green-400',
  Failed:              'bg-red-950 text-red-400',
}

const PRINT_AVAILABLE: JobStatus[] = [
  'SlicingComplete', 'GeneratingToolpaths', 'ToolpathsComplete',
  'PlanningHybrid', 'Ready', 'Running', 'Paused', 'Complete',
]
const TOOLPATH_AVAILABLE: JobStatus[] = [
  'ToolpathsComplete', 'PlanningHybrid', 'Ready', 'Running', 'Paused', 'Complete',
]
const HYBRID_AVAILABLE: JobStatus[] = [
  'Ready', 'Running', 'Paused', 'Complete',
]

interface SubItemProps {
  label: string
  icon: string
  href: string
  filename: string
  available: boolean
}

function SubItem({ label, icon, href, filename, available }: SubItemProps) {
  return (
    <div className={`flex items-center gap-2 px-3 py-1.5 rounded-lg border text-xs transition ${
      available
        ? 'bg-gray-800 border-gray-700 text-gray-300'
        : 'bg-gray-900 border-gray-800 text-gray-600'
    }`}>
      <span>{icon}</span>
      <span className="flex-1">{label}</span>
      {available ? (
        <a
          href={href}
          download={filename}
          onClick={e => e.stopPropagation()}
          className="px-2 py-0.5 rounded bg-gray-700 hover:bg-blue-800 hover:text-blue-200 text-gray-300 transition text-[10px] font-medium"
          title={`Download ${filename}`}
        >
          ⬇ Download
        </a>
      ) : (
        <span className="text-[10px] text-gray-700">not generated</span>
      )}
    </div>
  )
}

function JobCard({ job, onDelete, deleting }: { job: PrintJob; onDelete: () => void; deleting: boolean }) {
  const [expanded, setExpanded] = useState(false)

  const hasPrint    = PRINT_AVAILABLE.includes(job.status)
  const hasToolpath = TOOLPATH_AVAILABLE.includes(job.status)
  const hasHybrid   = HYBRID_AVAILABLE.includes(job.status)
  const hasAny      = hasPrint || hasToolpath || hasHybrid

  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl overflow-hidden">
      {/* Header row */}
      <div className="p-4 flex items-center justify-between">
        <div className="min-w-0 flex-1">
          <div className="flex items-center gap-2">
            {hasAny && (
              <button
                onClick={() => setExpanded(v => !v)}
                className="text-gray-500 hover:text-gray-300 transition text-xs shrink-0"
                title="Show / hide G-code files"
              >
                {expanded ? '▾' : '▸'}
              </button>
            )}
            <p className="font-medium text-white truncate">{job.name}</p>
          </div>
          <p className="text-xs text-gray-500 mt-0.5 pl-5">
            {new Date(job.createdAt).toLocaleString()}
            {job.totalPrintLayers !== undefined && ` · ${job.totalPrintLayers} layers`}
          </p>
          {job.errorMessage && (
            <p className="text-xs text-red-400 mt-0.5 pl-5 truncate">{job.errorMessage}</p>
          )}
        </div>
        <div className="flex items-center gap-3 flex-shrink-0 ml-4">
          <span className={`px-2.5 py-1 rounded-full text-xs font-medium ${STATUS_COLOR[job.status]}`}>
            {job.status}
          </span>
          <button
            onClick={() => {
              if (confirm(`Delete "${job.name}"?\n\nThis will permanently remove the job and all generated files (STL, G-code).`))
                onDelete()
            }}
            disabled={deleting}
            className="px-2.5 py-1 rounded-lg text-xs bg-gray-800 hover:bg-red-900/60 border border-gray-700 hover:border-red-700 text-gray-400 hover:text-red-300 transition disabled:opacity-40"
            title="Delete job and all generated files"
          >
            Delete
          </button>
        </div>
      </div>

      {/* Sub-items (G-code files) */}
      {expanded && hasAny && (
        <div className="px-4 pb-3 pt-0 grid grid-cols-1 sm:grid-cols-3 gap-2 border-t border-gray-800">
          <SubItem
            label="Extrusion G-code"
            icon="🖨️"
            href={`/api/jobs/${job.id}/print-gcode/download`}
            filename={`${job.name}_extrusion.gcode`}
            available={hasPrint}
          />
          <SubItem
            label="Toolpath G-code"
            icon="⚙️"
            href={`/api/jobs/${job.id}/toolpath-gcode`}
            filename={`${job.name}_toolpath.gcode`}
            available={hasToolpath}
          />
          <SubItem
            label="Hybrid G-code"
            icon="🔀"
            href={`/api/jobs/${job.id}/gcode`}
            filename={`${job.name}_hybrid.gcode`}
            available={hasHybrid}
          />
        </div>
      )}
    </div>
  )
}

export default function Dashboard() {
  const qc = useQueryClient()
  const { data: jobs = [], isLoading, isError } = useQuery({
    queryKey: ['jobs'],
    queryFn: jobsApi.getAll,
    refetchInterval: 3000,
  })

  const deleteMutation = useMutation({
    mutationFn: (id: string) => jobsApi.deleteJob(id),
    onSuccess: () => qc.invalidateQueries({ queryKey: ['jobs'] }),
  })

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <h2 className="text-2xl font-semibold text-white">Dashboard</h2>
        <Link
          to="/import"
          className="px-4 py-2 bg-primary/80 hover:bg-primary text-white text-sm rounded-lg transition-colors"
        >
          + New Job
        </Link>
      </div>

      {isLoading ? (
        <p className="text-gray-500">Loading jobs…</p>
      ) : isError ? (
        <p className="text-red-400">Failed to load jobs. Check your connection and try again.</p>
      ) : jobs.length === 0 ? (
        <div className="text-center py-20 text-gray-600">
          <p className="text-4xl mb-4">📦</p>
          <p>No jobs yet. Import an STL to get started.</p>
        </div>
      ) : (
        <div className="grid gap-4">
          {jobs.map(job => (
            <JobCard
              key={job.id}
              job={job}
              onDelete={() => deleteMutation.mutate(job.id)}
              deleting={deleteMutation.isPending}
            />
          ))}
        </div>
      )}
    </div>
  )
}
