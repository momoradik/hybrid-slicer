import { Link } from 'react-router-dom'

export default function NotFound() {
  return (
    <div className="flex flex-col items-center justify-center h-full gap-4 text-gray-500">
      <p className="text-6xl font-bold text-gray-700">404</p>
      <p className="text-lg">Page not found.</p>
      <Link to="/dashboard" className="text-primary/80 hover:underline text-sm">
        Back to Dashboard
      </Link>
    </div>
  )
}
