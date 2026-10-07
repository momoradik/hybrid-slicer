namespace HybridSlicer.Infrastructure.AlignMesh;

public sealed class AlignMeshOptions
{
    public const string Section = "AlignMesh";

    /// <summary>
    /// Path to the alignmesh_server executable.
    /// Can be absolute or relative to the API working directory.
    /// </summary>
    public string ExecutablePath { get; set; } = "alignmesh_server.exe";

    /// <summary>Localhost port for the alignmesh HTTP server.</summary>
    public int Port { get; set; } = 8001;

    /// <summary>Maximum seconds to wait for the server to become healthy on startup.</summary>
    public int StartupTimeoutSeconds { get; set; } = 30;
}
