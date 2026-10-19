-- wrk script: adds the bearer token from the TOKEN environment variable to every request.
local token = os.getenv("TOKEN")
wrk.headers["Authorization"] = "Bearer " .. token

done = function(summary, latency, requests)
  io.write(string.format("RESULT requests=%d errors_status=%d errors_socket=%d rps=%.1f p50_ms=%.2f p90_ms=%.2f p99_ms=%.2f max_ms=%.2f\n",
    summary.requests, summary.errors.status,
    summary.errors.connect + summary.errors.read + summary.errors.write + summary.errors.timeout,
    summary.requests / (summary.duration / 1e6),
    latency:percentile(50) / 1000, latency:percentile(90) / 1000,
    latency:percentile(99) / 1000, latency.max / 1000))
end
