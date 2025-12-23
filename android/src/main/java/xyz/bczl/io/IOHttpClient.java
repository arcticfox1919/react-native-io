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
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.channels.Channels;
import java.nio.channels.FileChannel;
import java.nio.channels.ReadableByteChannel;
import java.util.HashMap;
import java.util.Map;

/**
 * HTTP client implementation for React Native IO module.
 * Wraps HttpURLConnection with a simplified interface for JNI access.
 */
public class IOHttpClient {
    
    private static final int BUFFER_SIZE = 8192;
    
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
            connection.setConnectTimeout(timeoutMs);
            connection.setReadTimeout(timeoutMs);
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
        
        try {
            URL urlObj = new URL(url);
            connection = (HttpURLConnection) urlObj.openConnection();
            
            connection.setRequestMethod("GET");
            connection.setConnectTimeout(timeoutMs);
            connection.setReadTimeout(timeoutMs);
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
            if (resumable && destFile.exists()) {
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
            
            // Use NIO for efficient transfer
            try (InputStream is = connection.getInputStream();
                 FileOutputStream fos = new FileOutputStream(destinationPath, resumable)) {
                
                ReadableByteChannel srcChannel = Channels.newChannel(is);
                FileChannel destChannel = fos.getChannel();
                
                long transferred = destChannel.transferFrom(srcChannel, startPosition,
                        contentLength > 0 ? contentLength : Long.MAX_VALUE);
                
                srcChannel.close();
                destChannel.close();
                
                result.fileSize = startPosition + transferred;
            }
            
            result.filePath = destinationPath;
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
        
        try {
            URL urlObj = new URL(url);
            connection = (HttpURLConnection) urlObj.openConnection();
            
            connection.setRequestMethod("GET");
            connection.setConnectTimeout(timeoutMs);
            connection.setReadTimeout(timeoutMs);
            connection.setDoInput(true);
            
            if (headerKeys != null && headerValues != null) {
                for (int i = 0; i < headerKeys.length && i < headerValues.length; i++) {
                    connection.setRequestProperty(headerKeys[i], headerValues[i]);
                }
            }
            
            long startPosition = 0;
            File destFile = new File(destinationPath);
            if (resumable && destFile.exists()) {
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
            long totalSize = (result.statusCode == 206) ? 
                    contentLength + startPosition : contentLength;
            
            try (InputStream is = new BufferedInputStream(connection.getInputStream());
                 FileOutputStream fos = new FileOutputStream(destinationPath, resumable)) {
                
                byte[] buffer = new byte[BUFFER_SIZE];
                long totalBytesRead = startPosition;
                int bytesRead;
                
                while ((bytesRead = is.read(buffer)) != -1) {
                    fos.write(buffer, 0, bytesRead);
                    totalBytesRead += bytesRead;
                    
                    // Call native progress callback
                    if (nativeCallback != 0) {
                        double progress = totalSize > 0 ? 
                                (double) totalBytesRead / totalSize : 0.0;
                        nativeDownloadProgress(nativeCallback, totalBytesRead, totalSize, progress);
                    }
                }
                
                fos.flush();
                result.fileSize = totalBytesRead;
            }
            
            result.filePath = destinationPath;
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
            String boundary = "----IOHttpClientBoundary" + System.currentTimeMillis();
            
            URL urlObj = new URL(url);
            connection = (HttpURLConnection) urlObj.openConnection();
            
            connection.setRequestMethod("POST");
            connection.setConnectTimeout(timeoutMs);
            connection.setReadTimeout(timeoutMs);
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
                byte[] buffer = new byte[BUFFER_SIZE];
                int bytesRead;
                while ((bytesRead = fis.read(buffer)) != -1) {
                    os.write(buffer, 0, bytesRead);
                }
                
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
            String boundary = "----IOHttpClientBoundary" + System.currentTimeMillis();
            
            URL urlObj = new URL(url);
            connection = (HttpURLConnection) urlObj.openConnection();
            
            connection.setRequestMethod("POST");
            connection.setConnectTimeout(timeoutMs);
            connection.setReadTimeout(timeoutMs);
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
                
                long totalBytesWritten = 0;
                
                // Write prefix
                os.write(prefix);
                totalBytesWritten += prefix.length;
                
                // Write file content with progress
                byte[] buffer = new byte[BUFFER_SIZE];
                int bytesRead;
                while ((bytesRead = fis.read(buffer)) != -1) {
                    os.write(buffer, 0, bytesRead);
                    totalBytesWritten += bytesRead;
                    
                    if (nativeCallback != 0) {
                        double progress = (double) totalBytesWritten / totalSize;
                        nativeUploadProgress(nativeCallback, totalBytesWritten, totalSize, progress);
                    }
                }
                
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
