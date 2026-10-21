package io.example;

import android.app.Activity;
import android.os.Bundle;
import android.os.Debug;
import android.os.SystemClock;
import android.util.Log;
import android.widget.ScrollView;
import android.widget.TextView;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.*;
import java.net.*;
import java.nio.ByteBuffer;
import java.nio.channels.*;
import java.security.MessageDigest;
import java.util.*;
import xyz.bczl.io.IOHttpClient;

/** Test-only transport benchmark. Framework HTTP/TLS and normal hostname validation stay enabled. */
public final class HttpBenchmarkActivity extends Activity {
    private static final int BYTES = 4 * 1024 * 1024;
    private static final String HASH = "2b07811057df887086f06a67edc6ebf911de8b6741156e7a2eb1416a4b8b1b2e";
    private static final String[] STRATEGIES = {"stream-8k", "stream-64k", "stream-256k", "buffered-file-64k", "nio-coalesced-64k", "nio-transfer"};
    private TextView screen;
    private void progress(String message) {
        Log.i("IO_HTTP_BENCH", message);
        runOnUiThread(() -> screen.setText(message));
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        screen = new TextView(this);
        screen.setPadding(32, 60, 32, 32);
        screen.setTextSize(17);
        ScrollView scroll = new ScrollView(this); scroll.addView(screen); setContentView(scroll);
        new Thread(this::run, "IOHttpBenchmark").start();
    }
    private void run() {
        File root = new File(getCacheDir(), "http-benchmark-" + System.currentTimeMillis());
        JSONObject report = new JSONObject();
        JSONArray rows = new JSONArray();
        try {
            if (BuildConfig.DEBUG || !BuildConfig.IO_EXAMPLE_R8) throw new IOException("Requires non-debuggable Release/R8");
            if (!root.mkdirs()) throw new IOException("Cannot create benchmark workspace");
            File source = new File(root, "upload");
            File destination = new File(root, "download");
            byte[] block = new byte[65536];
            for (int i = 0; i < block.length; ++i) block[i] = (byte)i;
            try (FileOutputStream file = new FileOutputStream(source)) {
                for (int i = 0; i < BYTES / block.length; ++i) file.write(block);
            }
            if (!HASH.equals(hash(source))) throw new IOException("Source fixture mismatch");
            report.put("schemaVersion", 1).put("kind", "android-release-http-transport");
            report.put("model", android.os.Build.MODEL).put("api", android.os.Build.VERSION.SDK_INT);
            report.put("nativeDebuggable", BuildConfig.DEBUG).put("r8", BuildConfig.IO_EXAMPLE_R8);
            report.put("bytes", BYTES).put("sha256", HASH).put("warmup", 2).put("samples", 9).put("rounds", 4);
            report.put("timing", "connection, platform HTTP/TLS, transfer and close; no fsync; verification outside timer");
            report.put("cpu", "benchmark worker thread CPU time; excludes fixture/server CPU");
            report.put("rows", rows);
            if (getIntent().getBooleanExtra("productionOnly", false)) {
                report.put("kind", "android-release-production-http-validation");
                report.put("checks", validateProduction(source, destination));
                report.put("success", true);
                return;
            }
            for (int round = 0; round < 4; ++round) {
                String[] protocols = round % 2 == 0 ? new String[]{"http", "https"} : new String[]{"https", "http"};
                for (String protocol : protocols) {
                    String base = protocol + "://127.0.0.1:" + (protocol.equals("https") ? "8877" : "8876");
                    String[] directions = round % 2 == 0 ? new String[]{"download", "upload"} : new String[]{"upload", "download"};
                    for (String direction : directions) {
                        List<String> order = new ArrayList<>(Arrays.asList(STRATEGIES));
                        Collections.shuffle(order, new Random(0x1919 + round));
                        for (String strategy : order) {
                            progress("Release HTTP transport\nRound " + (round + 1) + "/4\n" + protocol + " " + direction + " " + strategy);
                            JSONArray wall = new JSONArray(), cpu = new JSONArray();
                            for (int sample = -2; sample < 9; ++sample) {
                                long cpuStart = Debug.threadCpuTimeNanos(), start = SystemClock.elapsedRealtimeNanos();
                                transfer(base, direction, strategy, source, destination);
                                double wallMs = (SystemClock.elapsedRealtimeNanos() - start) / 1e6;
                                double cpuMs = (Debug.threadCpuTimeNanos() - cpuStart) / 1e6;
                                if (direction.equals("download") && (!destination.isFile() || destination.length() != BYTES || !HASH.equals(hash(destination))))
                                    throw new IOException("Download data mismatch: " + strategy);
                                if (sample >= 0) { wall.put(wallMs); cpu.put(cpuMs); }
                            }
                            rows.put(new JSONObject().put("round", round + 1).put("protocol", protocol).put("direction", direction)
                                    .put("strategy", strategy).put("wallMs", wall).put("cpuMs", cpu));
                        }
                    }
                }
            }
            report.put("success", true);
        } catch (Throwable error) {
            try { report.put("success", false).put("error", Log.getStackTraceString(error)); } catch (Exception ignored) { }
        } finally {
            File[] files = root.listFiles(); if (files != null) for (File file : files) file.delete(); root.delete();
            try {
                File dir = new File(getExternalFilesDir("Documents"), "io-quality");
                if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("Cannot create report directory");
                File output = new File(dir, "http-transport-" + System.currentTimeMillis() + ".json");
                try (FileOutputStream stream = new FileOutputStream(output)) { stream.write(report.toString(2).getBytes("UTF-8")); }
                progress("HTTP transport benchmark finished\nSuccess: " + report.optBoolean("success") + "\n" + output + "\n" + report.optString("error", "All transfer hashes verified"));
            } catch (Exception error) { progress("Report failure: " + Log.getStackTraceString(error)); }
        }
    }
    private JSONArray validateProduction(File source, File destination) throws Exception {
        JSONArray checks = new JSONArray();
        for (String protocol : new String[]{"http", "https"}) {
            String base = protocol + "://127.0.0.1:" + (protocol.equals("https") ? "8877" : "8876");
            for (String endpoint : new String[]{"/payload", "/chunked"}) {
                progress("Production API validation: " + protocol + " " + endpoint);
                IOHttpClient.DownloadResult result = IOHttpClient.download(base + endpoint, destination.toString(), null, null, 10000, false);
                if (!result.success || result.fileSize != BYTES || !HASH.equals(hash(destination))) throw new IOException("Production download failed: " + result.errorMessage);
                result = IOHttpClient.downloadWithProgress(base + endpoint, destination.toString(), null, null, 10000, false, 0);
                if (!result.success || result.fileSize != BYTES || !HASH.equals(hash(destination))) throw new IOException("Production progress-download failed: " + result.errorMessage);
                checks.put(protocol + " " + endpoint + " download/progress/hash pass");
            }
            try (FileOutputStream file = new FileOutputStream(destination)) { file.write("keep".getBytes("UTF-8")); }
            if (IOHttpClient.download(base + "/truncated", destination.toString(), null, null, 10000, false).success || destination.length() != 4)
                throw new IOException("Production truncation accepted/destroyed target");
            try (FileInputStream input = new FileInputStream(destination)) {
                byte[] kept = new byte[4]; if (input.read(kept) != 4 || !Arrays.equals(kept, "keep".getBytes("UTF-8"))) throw new IOException("Old bytes changed");
            }
            checks.put(protocol + " truncated download preserves old bytes");
            IOHttpClient.UploadResult upload = IOHttpClient.upload(base + "/multipart", source.toString(), "file", "binary.bin", "application/octet-stream", null, null, null, null, 10000);
            if (!upload.success || !HASH.equals(new String(upload.responseBody, "UTF-8"))) throw new IOException("Production upload failed: " + upload.errorMessage);
            upload = IOHttpClient.uploadWithProgress(base + "/multipart", source.toString(), "file", "binary.bin", "application/octet-stream", null, null, null, null, 10000, 0);
            if (!upload.success || !HASH.equals(new String(upload.responseBody, "UTF-8"))) throw new IOException("Production progress-upload failed: " + upload.errorMessage);
            checks.put(protocol + " multipart upload/progress/server hash pass");
        }
        return checks;
    }
    private static String hash(File file) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256"); byte[] buffer = new byte[65536];
        try (FileInputStream input = new FileInputStream(file)) { int n; while ((n = input.read(buffer)) != -1) digest.update(buffer, 0, n); }
        StringBuilder text = new StringBuilder(); for (byte b : digest.digest()) text.append(String.format(Locale.US, "%02x", b & 255)); return text.toString();
    }
    private static void transfer(String base, String direction, String strategy, File source, File destination) throws Exception {
        HttpURLConnection connection = (HttpURLConnection)new URL(base + (direction.equals("upload") ? "/upload" : "/payload")).openConnection();
        connection.setConnectTimeout(10000); connection.setReadTimeout(10000); connection.setUseCaches(false);
        connection.setRequestProperty("Accept-Encoding", "identity");
        try {
            if (direction.equals("download")) {
                if (connection.getResponseCode() != 200 || connection.getContentLengthLong() != BYTES) throw new IOException("Invalid payload response");
                try (InputStream input = connection.getInputStream(); FileOutputStream output = new FileOutputStream(destination)) {
                    long received = copy(strategy, input, output, true);
                    if (received != BYTES) throw new IOException("Short download: " + received);
                }
            } else {
                connection.setRequestMethod("POST"); connection.setDoOutput(true); connection.setFixedLengthStreamingMode((long)BYTES);
                try (FileInputStream input = new FileInputStream(source); OutputStream output = connection.getOutputStream()) {
                    if (copy(strategy, input, output, false) != BYTES) throw new IOException("Short upload");
                }
                if (connection.getResponseCode() != 200) throw new IOException("Server upload check failed");
                ByteArrayOutputStream body = new ByteArrayOutputStream();
                try (InputStream input = connection.getInputStream()) { byte[] bytes = new byte[128]; int n; while ((n = input.read(bytes)) != -1) body.write(bytes, 0, n); }
                if (!HASH.equals(body.toString("UTF-8"))) throw new IOException("Server digest mismatch");
            }
        } finally { connection.disconnect(); }
    }
    private static long copy(String strategy, InputStream input, OutputStream output, boolean download) throws Exception {
        if (strategy.equals("buffered-file-64k")) {
            InputStream source = download ? input : new BufferedInputStream(input, 65536);
            OutputStream sink = download ? new BufferedOutputStream(output, 65536) : output;
            byte[] buffer = new byte[8192]; long total = 0; int n;
            while ((n = source.read(buffer)) != -1) {
                if (n == 0) throw new IOException("Read made no progress");
                sink.write(buffer, 0, n); total += n;
            }
            sink.flush();
            return total;
        }
        if (strategy.startsWith("stream-")) {
            int size = Integer.parseInt(strategy.substring(7, strategy.length() - 1)) * 1024;
            byte[] buffer = new byte[size]; long total = 0; int n;
            while ((n = input.read(buffer)) != -1) { if (n == 0) throw new IOException("Read made no progress"); output.write(buffer, 0, n); total += n; }
            return total;
        }
        ReadableByteChannel src = download ? Channels.newChannel(input) : ((FileInputStream)input).getChannel();
        WritableByteChannel dst = download ? ((FileOutputStream)output).getChannel() : Channels.newChannel(output);
        if (strategy.equals("nio-coalesced-64k")) {
            ByteBuffer buffer = ByteBuffer.allocateDirect(65536); long total = 0;
            for (;;) {
                int n = src.read(buffer);
                if (n == 0) throw new IOException("Channel read made no progress");
                if (n > 0) total += n;
                if (!buffer.hasRemaining() || n == -1) {
                    buffer.flip();
                    while (buffer.hasRemaining()) if (dst.write(buffer) == 0) throw new IOException("Channel write made no progress");
                    buffer.clear();
                }
                if (n == -1) break;
            }
            return total;
        }
        if (strategy.equals("nio-direct-64k")) {
            ByteBuffer buffer = ByteBuffer.allocateDirect(65536); long total = 0; int n;
            while ((n = src.read(buffer)) != -1) {
                if (n == 0) throw new IOException("Channel read made no progress");
                total += n; buffer.flip();
                while (buffer.hasRemaining()) if (dst.write(buffer) == 0) throw new IOException("Channel write made no progress");
                buffer.clear();
            }
            return total;
        }
        long total = 0;
        if (download) {
            FileChannel file = (FileChannel)dst;
            for (;;) {
                long n = file.transferFrom(src, total, BYTES - total);
                if (n > 0) { total += n; continue; }
                // A zero transfer is not proof of EOF. Probe the blocking source and handle short writes.
                ByteBuffer probe = ByteBuffer.allocate(1); int read = src.read(probe);
                if (read == -1) break;
                if (read == 0) throw new IOException("Channel made no progress");
                probe.flip(); file.position(total);
                while (probe.hasRemaining()) if (file.write(probe) == 0) throw new IOException("File write made no progress");
                total += read;
                if (total > BYTES) throw new IOException("Excess download bytes");
            }
        } else {
            FileChannel file = (FileChannel)src;
            while (total < BYTES) {
                long n = file.transferTo(total, BYTES - total, dst);
                if (n > 0) { total += n; continue; }
                ByteBuffer fallback = ByteBuffer.allocate(65536); file.position(total); int read = file.read(fallback);
                if (read <= 0) throw new IOException("Premature file end or no progress");
                fallback.flip(); while (fallback.hasRemaining()) if (dst.write(fallback) == 0) throw new IOException("Upload write made no progress");
                total += read;
            }
        }
        return total;
    }
}
