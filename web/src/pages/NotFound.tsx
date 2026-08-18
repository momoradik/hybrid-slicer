import { Link } from 'react-router-dom'

export default function NotFound() {
  return (
    <div className="flex flex-col items-center justify-center h-full gap-4">
      <div className="text-center">
        <p className="text-7xl font-bold text-gray-800 tabular-nums">404</p>
        <p className="text-lg text-gray-400 mt-2">This page doesn't exist.</p>
      </div>
      <Link to="/dashboard"
        className="px-4 py-2 bg-primary/20 hover:bg-primary/30 text-primary/80 text-sm rounded-lg transition">
        Back to Dashboard
      </Link>
    </div>
  )
}
