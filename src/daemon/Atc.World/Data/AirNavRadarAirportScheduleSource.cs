using System.Diagnostics;
using System.Globalization;
using Atc.World.Contracts.Data;

namespace Atc.World.Data;

public class AirNavRadarAirportScheduleSource : IAirportScheduleSource
{
    private readonly string _pythonExecutablePath;
    private readonly string _scriptPath;
    private readonly TimeSpan _timeout;
    private readonly bool _headless;

    public AirNavRadarAirportScheduleSource(
        string pythonExecutablePath,
        string scriptPath,
        TimeSpan timeout,
        bool headless = true)
    {
        _pythonExecutablePath = string.IsNullOrWhiteSpace(pythonExecutablePath)
            ? throw new ArgumentException("Python executable path is required.", nameof(pythonExecutablePath))
            : pythonExecutablePath;
        _scriptPath = string.IsNullOrWhiteSpace(scriptPath)
            ? throw new ArgumentException("Script path is required.", nameof(scriptPath))
            : scriptPath;
        _timeout = timeout <= TimeSpan.Zero
            ? throw new ArgumentOutOfRangeException(nameof(timeout), "Timeout must be positive.")
            : timeout;
        _headless = headless;
    }

    public async Task<AirportScheduleSnapshot> GetAirportSchedule(
        string airportIcao,
        CancellationToken cancellationToken = default)
    {
        var normalizedIcao = NormalizeAirportIcao(airportIcao);
        var scriptPath = Path.GetFullPath(_scriptPath);

        if (!File.Exists(scriptPath))
        {
            throw new FileNotFoundException(
                $"AirNavRadar schedule scraper script was not found at '{scriptPath}'.",
                scriptPath);
        }

        using var process = new Process
        {
            StartInfo = CreateStartInfo(scriptPath, normalizedIcao)
        };

        if (!process.Start())
        {
            throw new InvalidOperationException("Failed to start the AirNavRadar schedule scraper process.");
        }

        var stdoutTask = process.StandardOutput.ReadToEndAsync();
        var stderrTask = process.StandardError.ReadToEndAsync();

        using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutCts.CancelAfter(_timeout);

        try
        {
            await process.WaitForExitAsync(timeoutCts.Token);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            TryKill(process);
            throw new TimeoutException(
                $"Timed out after {_timeout.TotalSeconds.ToString("0", CultureInfo.InvariantCulture)}s while loading AirNavRadar schedules for {normalizedIcao}.");
        }

        var stdout = await stdoutTask;
        var stderr = await stderrTask;

        if (process.ExitCode != 0)
        {
            throw new InvalidOperationException(BuildFailureMessage(normalizedIcao, stderr, stdout));
        }

        if (string.IsNullOrWhiteSpace(stdout))
        {
            throw new InvalidOperationException(
                $"AirNavRadar schedule scraper returned no JSON output for {normalizedIcao}.");
        }

        return AirNavRadarAirportScheduleParser.Parse(stdout);
    }

    private ProcessStartInfo CreateStartInfo(string scriptPath, string normalizedIcao)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName = _pythonExecutablePath,
            WorkingDirectory = Path.GetDirectoryName(scriptPath) ?? AppContext.BaseDirectory,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true
        };

        startInfo.ArgumentList.Add(scriptPath);
        startInfo.ArgumentList.Add("--airport");
        startInfo.ArgumentList.Add(normalizedIcao);
        startInfo.ArgumentList.Add("--timeout-seconds");
        startInfo.ArgumentList.Add(Math.Ceiling(_timeout.TotalSeconds).ToString(CultureInfo.InvariantCulture));

        if (!_headless)
        {
            startInfo.ArgumentList.Add("--headed");
        }

        return startInfo;
    }

    private static string NormalizeAirportIcao(string airportIcao)
    {
        if (string.IsNullOrWhiteSpace(airportIcao))
        {
            throw new ArgumentException("Airport ICAO is required.", nameof(airportIcao));
        }

        return string.Concat(airportIcao.Where(c => !char.IsWhiteSpace(c))).ToUpperInvariant();
    }

    private static void TryKill(Process process)
    {
        if (process.HasExited)
        {
            return;
        }

        try
        {
            process.Kill(entireProcessTree: true);
        }
        catch (InvalidOperationException)
        {
            // Process already exited.
        }
    }

    private static string BuildFailureMessage(string airportIcao, string stderr, string stdout)
    {
        var details = stderr.Trim();
        if (string.IsNullOrWhiteSpace(details))
        {
            details = stdout.Trim();
        }

        if (details.Contains("No module named 'nodriver'", StringComparison.OrdinalIgnoreCase))
        {
            details +=
                " Install Python dependencies from tools/airnavradar/requirements.txt before running this command.";
        }

        return string.IsNullOrWhiteSpace(details)
            ? $"AirNavRadar schedule scraper failed for {airportIcao}."
            : $"AirNavRadar schedule scraper failed for {airportIcao}: {details}";
    }
}
