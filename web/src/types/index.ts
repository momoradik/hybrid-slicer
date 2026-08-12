export type JobStatus =
  | 'Draft' | 'StlImported' | 'Slicing' | 'SlicingComplete'
  | 'GeneratingToolpaths' | 'ToolpathsComplete' | 'PlanningHybrid'
  | 'Ready' | 'Running' | 'Paused' | 'Complete' | 'Failed'

export type MachineType = 'FDM' | 'CNC' | 'Hybrid'
export type ToolType = 'FlatEndMill' | 'BallEndMill' | 'BullNoseEndMill' | 'DrillBit' | 'Engraver' | 'Facemill' | 'Custom'
export type GCodeTrigger =
  | 'BeforeMachining' | 'AfterMachining' | 'BeforePrinting' | 'AfterPrinting' | 'JobStart' | 'JobEnd'
  | 'EveryNLayers'
  | 'BeforeExtruder0' | 'BeforeExtruder1' | 'BeforeExtruder2' | 'BeforeExtruder3'
  | 'BeforeExtruder4' | 'BeforeExtruder5' | 'BeforeExtruder6' | 'BeforeExtruder7'
  | 'AfterExtruder0' | 'AfterExtruder1' | 'AfterExtruder2' | 'AfterExtruder3'
  | 'AfterExtruder4' | 'AfterExtruder5' | 'AfterExtruder6' | 'AfterExtruder7'

export type OriginMode = 'BedFrontLeft' | 'BedCenter'

export interface MachineProfile {
  id: string
  name: string
  type: MachineType
  travelXMm: number
  travelYMm: number
  travelZMm: number
  originMode: OriginMode
  bedWidthMm: number
  bedDepthMm: number
  bedHeightMm: number
  bedPositionXMm: number
  bedPositionYMm: number
  originXMm: number
  originYMm: number
  bedCount: number
  bedsJson: string
  extruderCount: number
  nozzleXOffsetsJson: string
  nozzleYOffsetsJson: string
  leftBedEdgeOffsetMm: number
  rightBedEdgeOffsetMm: number
  frontBedEdgeOffsetMm: number
  backBedEdgeOffsetMm: number
  extruderAssignments: ExtruderAssignment[]
  ipAddress?: string
  port: number
  cncOffset: MachineOffset
  safeClearanceHeightMm: number
  extruderAxes: string
  cncAxes: string
  motionAssignmentEnabled: boolean
  motionAssignmentJson: string
  version: string
}

export interface BedDef {
  index: number
  widthMm: number
  depthMm: number
  heightMm: number
  positionXMm: number
  positionYMm: number
}

export interface ExtruderAssignment {
  extruderIndex: number
  duty: string
}

export const EXTRUDER_DUTIES = [
  'Walls',
  'Support',
  'Infill',
  'All',
] as const
export type ExtruderDuty = (typeof EXTRUDER_DUTIES)[number]

export interface MachineOffset {
  x: number
  y: number
  z: number
  rotationDeg: number
}

export interface PrintProfile {
  id: string
  name: string
  // Basic settings
  nozzleDiameterMm: number        // 0 = use machine default; machine_nozzle_size
  layerHeightMm: number           // layer_height
  lineWidthMm: number             // line_width (= nozzle diameter by default)
  materialFlowPct: number         // material_flow
  // Print speeds (mm/s)
  printSpeedMmS: number           // speed_print
  travelSpeedMmS: number          // speed_travel
  wallSpeedMmS: number            // speed_wall_0 (outer wall)
  innerWallSpeedMmS: number       // speed_wall_x (inner wall)
  infillSpeedMmS: number          // speed_infill
  topBottomSpeedMmS: number       // speed_topbottom
  firstLayerSpeedMmS: number      // speed_layer_0
  firstLayerTravelSpeedMmS: number // speed_travel_layer_0
  // Structure
  wallCount: number
  topLayers: number               // top_layers
  bottomLayers: number            // bottom_layers
  infillDensityPct: number
  infillPattern: string
  // Temperature (°C)
  printTemperatureDegC: number
  bedTemperatureDegC: number
  // Retraction
  retractionEnabled: boolean         // retraction_enable
  retractLengthMm: number
  retractSpeedMmS: number            // retraction_speed
  retractMinTravelMm: number         // retraction_min_travel
  // Cooling
  coolingEnabled: boolean
  coolingFanSpeedPct: number
  minLayerTimeSec: number            // cool_min_layer_time
  minSpeedMmS: number                // cool_min_speed
  // Motion control
  accelerationControlEnabled: boolean // acceleration_enabled
  jerkControlEnabled: boolean         // jerk_enabled
  // Surface quality
  skinMonotonic: boolean             // skin_monotonic
  // Support
  supportEnabled: boolean
  pelletModeEnabled: boolean
  virtualFilamentDiameterMm: number
  // Pellet / high-flow tuning — each maps onto a real CuraEngine key
  pressureAdvanceFactor: number          // material_pressure_advance_factor
  flowRateCompensationFactorPct: number  // flow_rate_extrusion_offset_factor
  flowRateMaxExtrusionOffsetMm: number   // flow_rate_max_extrusion_offset
  materialMaxFlowRateMm3S: number        // material_max_flowrate
  flowEqualizationRatioPct: number       // speed_equalize_flow_width_factor
  initialLayerFlowPct: number            // material_flow_layer_0
  standbyTemperatureDegC: number         // material_standby_temperature
  version: string
}

export interface CncTool {
  id: string
  name: string
  type: ToolType
  diameterMm: number
  fluteLengthMm: number
  /** Overall length from spindle collet face to tool tip (mm). Used for spindle clearance safety. */
  toolLengthMm: number
  shankDiameterMm: number
  fluteCount: number
  toolMaterial: string
  maxDepthOfCutMm: number
  recommendedRpm: number
  recommendedFeedMmPerMin: number
}

export interface PrintJob {
  id: string
  name: string
  stlFilePath: string
  status: JobStatus
  machineProfileId: string
  printProfileId: string
  materialId: string
  cncToolId?: string
  totalPrintLayers?: number
  printGCodePath?: string      // set after slicing
  toolpathGCodePath?: string   // set after generate-toolpaths
  hybridGCodePath?: string     // set after plan-hybrid
  errorMessage?: string
  createdAt: string
  updatedAt: string
}

export interface CustomGCodeBlock {
  id: string
  name: string
  gCodeContent: string
  trigger: GCodeTrigger
  description?: string
  isEnabled: boolean
  sortOrder: number
  /** null = shared block, offered on every machine. */
  machineProfileId?: string | null
  /** Cadence for the EveryNLayers trigger; 0 for all other triggers. */
  repeatEveryNLayers: number
  /** First layer the cadence may fire on (1-based). */
  startLayer: number
  /** Last layer the cadence may fire on; null = to the end of the print. */
  endLayer?: number | null
}

/** Error envelope returned by the API's GlobalExceptionMiddleware. */
export interface ApiError {
  code: string
  message: string
  title?: string
  detail?: string
  /** Name of the offending field, e.g. "layerHeightMm". */
  field?: string | null
  providedValue?: string | null
  expected?: string | null
}

export interface BrandingSettings {
  companyName: string
  appTitle: string
  logoUrl?: string
  primaryColor: string
  accentColor: string
  supportEmail?: string
}

export interface Material {
  id: string
  name: string
  type: string
  printTempMinDegC: number
  printTempMaxDegC: number
  bedTempMinDegC: number
  bedTempMaxDegC: number
  diameterMm: number
}
