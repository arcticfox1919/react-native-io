/**
 * IOHttpClient.java
 * 
 * A simple HTTP client wrapper for Android using HttpURLConnection.
 * Provides a clean interface for C++ JNI calls.
 *
 * Copyright (c) 2025 arcticfox
 */

package xyz.bczl.io;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.HashMap;
import java.util.Map;

/**
 * HTTP client implementation for React Native IO module.
 * Wraps HttpURLConnection with a simplified interface for JNI access.
 */
public class IOHttpClient {
    
    // Batch file writes without adapting platform HTTP streams into channels.
    // Release strategy measurements and tradeoffs are in docs/http-performance-2026-10-21.md.
    private static final int BUFFER_SIZE = 8192; // Response accumulation and download reads.
    private static final int DOWNLOAD_BUFFER_SIZE = 64 * 1024;
    private static final int UPLOAD_BUFFER_SIZE = 256 * 1024;

    private static int downloadBufferSize(long expected) {
        return expected >= 0 ? (int)Math.max(1, Math.min(DOWNLOAD_BUFFER_SIZE, expected)) : DOWNLOAD_BUFFER_SIZE;
    }

    private static void configureTimeouts(HttpURLConnection connection, int timeoutMs) {
        if (timeoutMs <= 0) {
            throw new IllegalArgumentException("timeoutMs must be greater than zero");
        }
        connection.setConnectTimeout(timeoutMs);
        connection.setReadTimeout(timeoutMs);
    }

    private static long receiveFile(InputStream input, BufferedOutputStream output,
                                    long start, long expected, long callback) throws IOException {
        int capacity = downloadBufferSize(expected);
        // Both buffers are operation-local; small known-length transfers stay small.
        byte[] buffer = new byte[Math.min(BUFFER_SIZE, capacity)];
        long received = 0;
        long reported = 0;
        long totalSize = expected >= 0 ? start + expected : 0;
        int count;
        while ((count = input.read(buffer)) != -1) {
            if (count == 0) throw new IOException("Download stream made no progress");
            output.write(buffer, 0, count);
            received += count;
            if (callback != 0 && received - reported >= capacity) {
                // Report bytes only after they leave the Java file buffer, not merely after reading.
                output.flush();
                long current = start + received;
                nativeDownloadProgress(callback, current, totalSize, totalSize > 0 ? (double)current / totalSize : 0.0);
                reported = received;
            }
        }
        if (expected >= 0 && received != expected) throw new IOException("Incomplete download");
        output.flush(); // Includes the non-aligned tail; a flush failure prevents commit.
        if (callback != 0 && received > reported) {
            long current = start + received;
            nativeDownloadProgress(callback, current, totalSize, totalSize > 0 ? (double)current / totalSize : 0.0);
        }
        // Caller closes all streams before committing the staged file; flush is not fsync.
        return start + received;
    }

    private static long sendFile(FileInputStream input, OutputStream output,
                                 long expected, long prefixSize, long totalSize,
                                 long callback) throws IOException {
        // Keep small transfers small; large files amortize filesystem and stream calls.
        int capacity = (int)Math.max(1, Math.min(UPLOAD_BUFFER_SIZE, expected));
        byte[] buffer = new byte[capacity];
        long sent = 0;
        int count;
        while ((count = input.read(buffer)) != -1) {
            if (count == 0) throw new IOException("Upload file read made no progress");
            output.write(buffer, 0, count);
            sent += count;
            if (callback != 0) {
                long current = prefixSize + sent;
                nativeUploadProgress(callback, current, totalSize, totalSize > 0 ? (double)current / totalSize : 0.0);
            }
        }
        if (sent != expected) throw new IOException("Upload source length changed during transfer");
        return sent;
    }

    private static boolean hasResumeValidator(String[] keys, String[] values) {
        if (keys == null || values == null) return false;
        for (int i = 0; i < Math.min(keys.length, values.length); ++i)
            if ("If-Range".equalsIgnoreCase(keys[i]) && values[i] != null && !values[i].isEmpty()) return true;
        return false;
    }
    private static void validateResumeRange(HttpURLConnection connection, long start, long length) throws IOException {
        String header = connection.getHeaderField("Content-Range");
        if (header == null) throw new IOException("Missing Content-Range");
        java.util.regex.Matcher match = java.util.regex.Pattern.compile("bytes (\\d+)-(\\d+)/(\\d+)").matcher(header);
        if (!match.matches()) throw new IOException("Invalid Content-Range");
        long first = Long.parseLong(match.group(1)), last = Long.parseLong(match.group(2)), total = Long.parseLong(match.group(3));
        if (first != start || last < first || total <= last || last != total - 1 || (length >= 0 && length != last - first + 1))
            throw new IOException("Content-Range does not match the requested remainder");
    }
    private static void validateMultipartName(String value) throws IOException {
        if (value == null || value.indexOf('\r') >= 0 || value.indexOf('\n') >= 0 || value.indexOf('"') >= 0 || value.indexOf('\0') >= 0) {
            throw new IOException("Invalid multipart field/file name or MIME type");
        }
    }

    
    // ========================================================================
    // Response Data Class
    // ========================================================================
    
    /**
     * HTTP response data holder.
     * All fields are public for easy JNI access.
     */
    public static class HttpResult {
        public boolean success = false;
        public int statusCode = 0;
        public String statusMessage = "";
        public String finalUrl = "";
        public String errorMessage = "";
        public byte[] body = new byte[0];
        public String[] headerKeys = new String[0];
        public String[] headerValues = new String[0];
    }
    
    /**
     * Download result data holder.
     */
    public static class DownloadResult {
        public boolean success = false;
        public int statusCode = 0;
        public String filePath = "";
        public long fileSize = 0;
        public String errorMessage = "";
    }
    
    /**
     * Upload result data holder.
     */
    public static class UploadResult {
        public boolean success = false;
        public int statusCode = 0;
        public byte[] responseBody = new byte[0];
        public String errorMessage = "";
    }
    
    /**
     * Progress callback interface.
     */
    public interface ProgressCallback {
        void onProgress(long current, long total, double progress);
    }
    
    // ========================================================================
    // HTTP Request
    // ========================================================================
    
    /**
     * Perform an HTTP request.
     *
     * @param url             Request URL
     * @param method          HTTP method (GET, POST, PUT, DELETE, etc.)
     * @param headerKeys      Request header keys
     * @param headerValues    Request header values
     * @param body            Request body (can be null)
     * @param timeoutMs       Timeout in milliseconds
     * @param followRedirects Whether to follow redirects
     * @return HttpResult containing response data
     */
    public static HttpResult request(
            String url,
            String method,
            String[] headerKeys,
            String[] headerValues,
            byte[] body,
            int timeoutMs,
            boolean followRedirects) {
        
        HttpResult result = new HttpResult();
        HttpURLConnection connection = null;
        
        try {
            URL urlObj = new URL(url);
            connection = (HttpURLConnection) urlObj.openConnection();
            
            // Configure connection
            connection.setRequestMethod(method);
            configureTimeouts(connection, timeoutMs);
            connection.setInstanceFollowRedirects(followRedirects);
            
            // Set headers
            if (headerKeys != null && headerValues != null) {
                for (int i = 0; i < headerKeys.length && i < headerValues.length; i++) {
                    connection.setRequestProperty(headerKeys[i], headerValues[i]);
                }
            }
            
            // Write body if present
            if (body != null && body.length > 0) {
                connection.setDoOutput(true);
                connection.setFixedLengthStreamingMode(body.length);
                try (OutputStream os = connection.getOutputStream()) {
                    os.write(body);
                    os.flush();
                }
            }
            
            // Connect
            connection.connect();
            
            // Read response
            result.statusCode = connection.getResponseCode();
            result.statusMessage = connection.getResponseMessage();
            result.finalUrl = connection.getURL().toString();
            
            // Read headers
            Map<String, String> headers = new HashMap<>();
            for (int i = 0; ; i++) {
                String key = connection.getHeaderFieldKey(i);
                String value = connection.getHeaderField(i);
                if (value == null) break;
                if (key != null) {
                    headers.put(key, value);
                }
            }
            result.headerKeys = headers.keySet().toArray(new String[0]);
            result.headerValues = headers.values().toArray(new String[0]);
            
            // Read body
            result.body = readResponseBody(connection);
            result.success = true;
            
        } catch (Exception e) {
            result.errorMessage = e.getClass().getSimpleName() + ": " + e.getMessage();
        } finally {
            if (connection != null) {
                connection.disconnect();
            }
        }
        
        return result;
    }
    
    // ========================================================================
    // File Download
    // ========================================================================
    
    /**
     * Download a file from URL.
     *
     * @param url             Download URL
     * @param destinationPath Local file path to save
     * @param headerKeys      Request header keys
     * @param headerValues    Request header values
     * @param timeoutMs       Timeout in milliseconds
     * @param resumable       Whether to resume from existing file
     * @return DownloadResult containing download status
     */
    public static DownloadResult download(
            String url,
            String destinationPath,
            String[] headerKeys,
            String[] headerValues,
            int timeoutMs,
            boolean resumable) {
        
        DownloadResult result = new DownloadResult();
        HttpURLConnection connection = null;
        File staged = null;
        
        try {
            URL urlObj = new URL(url);
            connection = (HttpURLConnection) urlObj.openConnection();
            
            connection.setRequestMethod("GET");
            configureTimeouts(connection, timeoutMs);
            connection.setDoInput(true);
            
            // Set headers
            if (headerKeys != null && headerValues != null) {
                for (int i = 0; i < headerKeys.length && i < headerValues.length; i++) {
                    connection.setRequestProperty(headerKeys[i], headerValues[i]);
                }
            }
            
            // Handle resume
            long startPosition = 0;
            File destFile = new File(destinationPath);
            if (resumable && destFile.exists() && hasResumeValidator(headerKeys, headerValues)) {
                startPosition = destFile.length();
                connection.setRequestProperty("Range", "bytes=" + startPosition + "-");
            }
            
            connection.connect();
            
            result.statusCode = connection.getResponseCode();
            
            if (result.statusCode != 200 && result.statusCode != 206) {
                result.errorMessage = "HTTP error: " + result.statusCode;
                return result;
            }
            
            long contentLength = connection.getContentLengthLong();
            
            boolean append = result.statusCode == 206 && startPosition > 0;
            if (result.statusCode == 206) validateResumeRange(connection, startPosition, contentLength);
            if (!append) startPosition = 0; // Server ignored Range: restart, never append full response.
            File parent = destFile.getAbsoluteFile().getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create destination directory");
            File outputFile = append ? destFile : File.createTempFile(".rn-io-", ".download", parent);
            if (!append) staged = outputFile;
            try (InputStream input = connection.getInputStream();
                 FileOutputStream output = new FileOutputStream(outputFile, append);
                 BufferedOutputStream buffered = new BufferedOutputStream(output, downloadBufferSize(contentLength))) {
                result.fileSize = receiveFile(input, buffered, startPosition, contentLength, 0);
            }

            if (staged != null) {
                if (!staged.renameTo(destFile)) throw new IOException("Cannot commit completed download");
                staged = null;
            }
            result.filePath = destinationPath;
            result.success = true;
            
        } catch (Exception e) {
            result.errorMessage = e.getClass().getSimpleName() + ": " + e.getMessage();
        } finally {
            if (staged != null) staged.delete();
            if (connection != null) {
                connection.disconnect();
            }
        }
        
        return result;
    }
    
    /**
     * Download a file with progress callback (for JNI native callback).
     * This version uses manual loop for progress reporting.
     */
    public static DownloadResult downloadWithProgress(
            String url,
            String destinationPath,
            String[] headerKeys,
            String[] headerValues,
            int timeoutMs,
            boolean resumable,
            long nativeCallback) {
        
        DownloadResult result = new DownloadResult();
        HttpURLConnection connection = null;
        File staged = null;
        
        try {
            URL urlObj = new URL(url);
            connection = (HttpURLConnection) urlObj.openConnection();
            
            connection.setRequestMethod("GET");
            configureTimeouts(connection, timeoutMs);
            connection.setDoInput(true);
            
            if (headerKeys != null && headerValues != null) {
                for (int i = 0; i < headerKeys.length && i < headerValues.length; i++) {
                    connection.setRequestProperty(headerKeys[i], headerValues[i]);
                }
            }
            
            long startPosition = 0;
            File destFile = new File(destinationPath);
            if (resumable && destFile.exists() && hasResumeValidator(headerKeys, headerValues)) {
                startPosition = destFile.length();
                connection.setRequestProperty("Range", "bytes=" + startPosition + "-");
            }
            
            connection.connect();
            
            result.statusCode = connection.getResponseCode();
            
            if (result.statusCode != 200 && result.statusCode != 206) {
                result.errorMessage = "HTTP error: " + result.statusCode;
                return result;
            }
            
            long contentLength = connection.getContentLengthLong();
            boolean append = result.statusCode == 206 && startPosition > 0;
            if (result.statusCode == 206) validateResumeRange(connection, startPosition, contentLength);
            if (!append) startPosition = 0;
            File parent = destFile.getAbsoluteFile().getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create destination directory");
            long totalSize = (result.statusCode == 206) ? 
                    contentLength + startPosition : contentLength;
            
            File outputFile = append ? destFile : File.createTempFile(".rn-io-", ".download", parent);
            if (!append) staged = outputFile;
            try (InputStream is = connection.getInputStream();
                 FileOutputStream fos = new FileOutputStream(outputFile, append);
                 BufferedOutputStream buffered = new BufferedOutputStream(fos, downloadBufferSize(contentLength))) {
                result.fileSize = receiveFile(is, buffered, startPosition, contentLength, nativeCallback);
            }
            
            if (staged != null) {
                if (!staged.renameTo(destFile)) throw new IOException("Cannot commit completed download");
                staged = null;
            }
            result.filePath = destinationPath;
            result.success = true;
            
        } catch (Exception e) {
            result.errorMessage = e.getClass().getSimpleName() + ": " + e.getMessage();
        } finally {
            if (staged != null) staged.delete();
            if (connection != null) {
                connection.disconnect();
            }
        }
        
        return result;
    }
    
    // ========================================================================
    // File Upload (Multipart)
    // ========================================================================
    
    /**
     * Upload a file using multipart/form-data.
     *
     * @param url           Upload URL
     * @param filePath      Local file path to upload
     * @param fieldName     Form field name for the file
     * @param fileName      File name in the form
     * @param mimeType      MIME type of the file
     * @param headerKeys    Request header keys
     * @param headerValues  Request header values
     * @param formKeys      Additional form field keys
     * @param formValues    Additional form field values
     * @param timeoutMs     Timeout in milliseconds
     * @return UploadResult containing upload status
     */
    public static UploadResult upload(
            String url,
            String filePath,
            String fieldName,
            String fileName,
            String mimeType,
            String[] headerKeys,
            String[] headerValues,
            String[] formKeys,
            String[] formValues,
            int timeoutMs) {
        
        UploadResult result = new UploadResult();
        HttpURLConnection connection = null;
        
        try {
            File file = new File(filePath);
            if (!file.exists()) {
                result.errorMessage = "File not found: " + filePath;
                return result;
            }
            
            long fileSize = file.length();
            String boundary = "----IOHttpClientBoundary" + java.util.UUID.randomUUID();
            
            URL urlObj = new URL(url);
            connection = (HttpURLConnection) urlObj.openConnection();
            
            connection.setRequestMethod("POST");
            configureTimeouts(connection, timeoutMs);
            connection.setDoOutput(true);
            connection.setDoInput(true);
            connection.setRequestProperty("Content-Type", "multipart/form-data; boundary=" + boundary);
            
            // Set additional headers
            if (headerKeys != null && headerValues != null) {
                for (int i = 0; i < headerKeys.length && i < headerValues.length; i++) {
                    connection.setRequestProperty(headerKeys[i], headerValues[i]);
                }
            }
            
            // Resolve file name
            String actualFileName = (fileName == null || fileName.isEmpty()) ? 
                    file.getName() : fileName;
            String actualMimeType = (mimeType == null || mimeType.isEmpty()) ? 
                    "application/octet-stream" : mimeType;
            
            // Build prefix
            validateMultipartName(fieldName);
            validateMultipartName(actualFileName);
            validateMultipartName(actualMimeType);
            if (formKeys != null) for (String key : formKeys) validateMultipartName(key);
            StringBuilder prefixBuilder = new StringBuilder();
            
            // Add form fields
            if (formKeys != null && formValues != null) {
                for (int i = 0; i < formKeys.length && i < formValues.length; i++) {
                    prefixBuilder.append("--").append(boundary).append("\r\n");
                    prefixBuilder.append("Content-Disposition: form-data; name=\"")
                            .append(formKeys[i]).append("\"\r\n\r\n");
                    prefixBuilder.append(formValues[i]).append("\r\n");
                }
            }
            
            // Add file part header
            prefixBuilder.append("--").append(boundary).append("\r\n");
            prefixBuilder.append("Content-Disposition: form-data; name=\"")
                    .append(fieldName).append("\"; filename=\"")
                    .append(actualFileName).append("\"\r\n");
            prefixBuilder.append("Content-Type: ").append(actualMimeType).append("\r\n\r\n");
            
            byte[] prefix = prefixBuilder.toString().getBytes("UTF-8");
            byte[] suffix = ("\r\n--" + boundary + "--\r\n").getBytes("UTF-8");
            
            long totalSize = prefix.length + fileSize + suffix.length;
            connection.setFixedLengthStreamingMode(totalSize);
            
            connection.connect();
            
            try (OutputStream os = connection.getOutputStream();
                 FileInputStream fis = new FileInputStream(file)) {
                
                // Write prefix
                os.write(prefix);
                
                // Write file content
                sendFile(fis, os, fileSize, prefix.length, totalSize, 0);

                // Write suffix
                os.write(suffix);
                os.flush();
            }
            
            result.statusCode = connection.getResponseCode();
            result.responseBody = readResponseBody(connection);
            result.success = (result.statusCode >= 200 && result.statusCode < 300);
            
        } catch (Exception e) {
            result.errorMessage = e.getClass().getSimpleName() + ": " + e.getMessage();
        } finally {
            if (connection != null) {
                connection.disconnect();
            }
        }
        
        return result;
    }
    
    /**
     * Upload with progress callback.
     */
    public static UploadResult uploadWithProgress(
            String url,
            String filePath,
            String fieldName,
            String fileName,
            String mimeType,
            String[] headerKeys,
            String[] headerValues,
            String[] formKeys,
            String[] formValues,
            int timeoutMs,
            long nativeCallback) {
        
        UploadResult result = new UploadResult();
        HttpURLConnection connection = null;
        
        try {
            File file = new File(filePath);
            if (!file.exists()) {
                result.errorMessage = "File not found: " + filePath;
                return result;
            }
            
            long fileSize = file.length();
            String boundary = "----IOHttpClientBoundary" + java.util.UUID.randomUUID();
            
            URL urlObj = new URL(url);
            connection = (HttpURLConnection) urlObj.openConnection();
            
            connection.setRequestMethod("POST");
            configureTimeouts(connection, timeoutMs);
            connection.setDoOutput(true);
            connection.setDoInput(true);
            connection.setRequestProperty("Content-Type", "multipart/form-data; boundary=" + boundary);
            
            if (headerKeys != null && headerValues != null) {
                for (int i = 0; i < headerKeys.length && i < headerValues.length; i++) {
                    connection.setRequestProperty(headerKeys[i], headerValues[i]);
                }
            }
            
            String actualFileName = (fileName == null || fileName.isEmpty()) ? 
                    file.getName() : fileName;
            String actualMimeType = (mimeType == null || mimeType.isEmpty()) ? 
                    "application/octet-stream" : mimeType;
            
            validateMultipartName(fieldName);
            validateMultipartName(actualFileName);
            validateMultipartName(actualMimeType);
            if (formKeys != null) for (String key : formKeys) validateMultipartName(key);
            StringBuilder prefixBuilder = new StringBuilder();
            
            if (formKeys != null && formValues != null) {
                for (int i = 0; i < formKeys.length && i < formValues.length; i++) {
                    prefixBuilder.append("--").append(boundary).append("\r\n");
                    prefixBuilder.append("Content-Disposition: form-data; name=\"")
                            .append(formKeys[i]).append("\"\r\n\r\n");
                    prefixBuilder.append(formValues[i]).append("\r\n");
                }
            }
            
            prefixBuilder.append("--").append(boundary).append("\r\n");
            prefixBuilder.append("Content-Disposition: form-data; name=\"")
                    .append(fieldName).append("\"; filename=\"")
                    .append(actualFileName).append("\"\r\n");
            prefixBuilder.append("Content-Type: ").append(actualMimeType).append("\r\n\r\n");
            
            byte[] prefix = prefixBuilder.toString().getBytes("UTF-8");
            byte[] suffix = ("\r\n--" + boundary + "--\r\n").getBytes("UTF-8");
            
            long totalSize = prefix.length + fileSize + suffix.length;
            connection.setFixedLengthStreamingMode(totalSize);
            
            connection.connect();
            
            try (OutputStream os = connection.getOutputStream();
                 FileInputStream fis = new FileInputStream(file)) {
                
                // Write prefix
                os.write(prefix);
                
                // Write file content with progress
                sendFile(fis, os, fileSize, prefix.length, totalSize, nativeCallback);

                // Write suffix
                os.write(suffix);
                os.flush();
            }
            
            result.statusCode = connection.getResponseCode();
            result.responseBody = readResponseBody(connection);
            result.success = (result.statusCode >= 200 && result.statusCode < 300);
            
        } catch (Exception e) {
            result.errorMessage = e.getClass().getSimpleName() + ": " + e.getMessage();
        } finally {
            if (connection != null) {
                connection.disconnect();
            }
        }
        
        return result;
    }
    
    // ========================================================================
    // Helper Methods
    // ========================================================================
    
    /**
     * Read response body from connection (handles both success and error streams).
     */
    private static byte[] readResponseBody(HttpURLConnection connection) throws IOException {
        InputStream is;
        try {
            is = connection.getInputStream();
        } catch (IOException e) {
            is = connection.getErrorStream();
        }
        
        if (is == null) {
            return new byte[0];
        }
        
        try (BufferedInputStream bis = new BufferedInputStream(is)) {
            java.io.ByteArrayOutputStream baos = new java.io.ByteArrayOutputStream();
            byte[] buffer = new byte[BUFFER_SIZE];
            int bytesRead;
            while ((bytesRead = bis.read(buffer)) != -1) {
                if ((long) baos.size() + bytesRead > 256L * 1024 * 1024) throw new IOException("HTTP response exceeds 256 MiB; use download");
                baos.write(buffer, 0, bytesRead);
            }
            return baos.toByteArray();
        }
    }
    
    // ========================================================================
    // Native Progress Callbacks (implemented in C++)
    // ========================================================================
    
    private static native void nativeDownloadProgress(long callback, long current, long total, double progress);
    private static native void nativeUploadProgress(long callback, long current, long total, double progress);
}
