import com.sun.net.httpserver.HttpServer;
import java.net.InetSocketAddress;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import xyz.bczl.io.IOHttpClient;

public class AndroidHttpTest {
  static void check(boolean result,String message){if(!result)throw new AssertionError(message);}
  public static void main(String[] args)throws Exception{
    HttpServer server=HttpServer.create(new InetSocketAddress("127.0.0.1",0),0);
    server.createContext("/full",e->{byte[] b="complete".getBytes();e.sendResponseHeaders(200,b.length);e.getResponseBody().write(b);e.close();});
    server.createContext("/range",e->{byte[] b="plete".getBytes();e.getResponseHeaders().add("Content-Range","bytes 3-7/8");e.sendResponseHeaders(206,b.length);e.getResponseBody().write(b);e.close();});
    server.createContext("/bad",e->{byte[] b="bad".getBytes();e.getResponseHeaders().add("Content-Range","bytes 99-101/102");e.sendResponseHeaders(206,b.length);e.getResponseBody().write(b);e.close();});
    server.createContext("/incomplete-range",e->{byte[] b="ple".getBytes();e.getResponseHeaders().add("Content-Range","bytes 3-5/8");e.sendResponseHeaders(206,b.length);e.getResponseBody().write(b);e.close();});
    server.createContext("/short",e->{e.sendResponseHeaders(200,10);e.getResponseBody().write(new byte[]{1,2,3});e.close();});
    server.createContext("/upload",e->{String body=new String(e.getRequestBody().readAllBytes(),StandardCharsets.UTF_8);check(body.contains("name=\"caption\"")&&body.contains("hello")&&body.contains("filedata"),"multipart fields lost");e.sendResponseHeaders(200,0);e.close();});
    byte[] large = new byte[1024 * 1024 + 3];
    for (int i = 0; i < large.length; ++i) large[i] = (byte)(i * 31 + 7);
    server.createContext("/large", e -> { e.sendResponseHeaders(200, large.length); e.getResponseBody().write(large); e.close(); });
    server.createContext("/chunked", e -> {
      e.sendResponseHeaders(200, 0);
      for (int start = 0; start < large.length; start += 4093)
        e.getResponseBody().write(large, start, Math.min(4093, large.length - start));
      e.close();
    });
    server.createContext("/short-large", e -> { e.sendResponseHeaders(200, large.length + 8192); e.getResponseBody().write(large, 0, large.length - 13); e.close(); });
    server.createContext("/upload-large", e -> {
      byte[] body = e.getRequestBody().readAllBytes();
      String headers = new String(body, StandardCharsets.ISO_8859_1);
      int marker = headers.indexOf("filename=\"binary.bin\"");
      int start = marker < 0 ? -1 : headers.indexOf("\r\n\r\n", marker);
      check(start >= 0 && body.length > start + 4 + large.length, "large multipart header/body missing");
      check(java.util.Arrays.equals(large, java.util.Arrays.copyOfRange(body, start + 4, start + 4 + large.length)), "large multipart binary mismatch");
      e.sendResponseHeaders(204, -1); e.close();
    });
    server.start();
    Path directory=Files.createTempDirectory("rn-io-http");Path dest=directory.resolve("output");
    String base="http://127.0.0.1:"+server.getAddress().getPort();
    try {
      long zeroTimeoutStart = System.nanoTime();
      check(!IOHttpClient.download(base+"/full",dest.toString(),null,null,0,false).success,"zero timeout accepted");
      check(System.nanoTime()-zeroTimeoutStart < 1_000_000_000L,"zero timeout did not fail promptly");
      Files.writeString(dest,"com");
      check(IOHttpClient.download(base+"/full",dest.toString(),new String[]{"If-Range"},new String[]{"\"v1\""},1000,true).success,"ignored range download failed");
      check(Files.readString(dest).equals("complete"),"server ignoring Range must replace, not append");
      Files.writeString(dest,"com");
      check(IOHttpClient.downloadWithProgress(base+"/range",dest.toString(),new String[]{"If-Range"},new String[]{"\"v1\""},1000,true,0).success,"valid range failed");
      check(Files.readString(dest).equals("complete"),"incorrect range contents");
      Files.writeString(dest,"com");
      check(!IOHttpClient.download(base+"/incomplete-range",dest.toString(),new String[]{"If-Range"},new String[]{"\"v1\""},1000,true).success,"incomplete remainder accepted");
      check(Files.readString(dest).equals("com"),"incomplete range changed destination");
      Files.writeString(dest,"original");
      check(!IOHttpClient.download(base+"/bad",dest.toString(),new String[]{"If-Range"},new String[]{"\"v1\""},1000,true).success,"bad range accepted");
      check(Files.readString(dest).equals("original"),"bad range changed destination");
      check(!IOHttpClient.download(base+"/short",dest.toString(),null,null,1000,false).success,"truncated transfer accepted");
      check(Files.readString(dest).equals("original"),"failed transfer destroyed existing file");
      Path input=directory.resolve("input");Files.writeString(input,"filedata");
      check(IOHttpClient.upload(base+"/upload",input.toString(),"file","input.txt","text/plain",null,null,new String[]{"caption"},new String[]{"hello"},1000).success,"multipart upload failed");
      check(!IOHttpClient.upload(base+"/upload",input.toString(),"bad\r\nfield","input.txt","text/plain",null,null,null,null,1000).success,"multipart injection accepted");
      for (String endpoint : new String[]{"/large", "/chunked"}) {
        check(IOHttpClient.download(base + endpoint, dest.toString(), null, null, 3000, false).success, "large/chunked download failed");
        check(java.util.Arrays.equals(large, Files.readAllBytes(dest)), "large/chunked bytes mismatch");
        check(IOHttpClient.downloadWithProgress(base + endpoint, dest.toString(), null, null, 3000, false, 0).success, "progress large/chunked download failed");
        check(java.util.Arrays.equals(large, Files.readAllBytes(dest)), "progress large/chunked bytes mismatch");
      }
      Files.writeString(dest, "keep-large-failure");
      check(!IOHttpClient.download(base + "/short-large", dest.toString(), null, null, 3000, false).success, "large truncation accepted");
      check(Files.readString(dest).equals("keep-large-failure"), "large truncation changed target");
      Files.write(input, large);
      check(IOHttpClient.upload(base + "/upload-large", input.toString(), "file", "binary.bin", "application/octet-stream", null, null, null, null, 3000).success, "large upload failed");
      check(IOHttpClient.uploadWithProgress(base + "/upload-large", input.toString(), "file", "binary.bin", "application/octet-stream", null, null, null, null, 3000, 0).success, "large progress upload failed");
      System.out.println("PASS: Android HTTP ignored/valid/invalid ranges, incomplete transfer preservation, multipart upload/injection");
    }finally{server.stop(0);try(var files=Files.list(directory)){for(Path file:files.toList())Files.deleteIfExists(file);}Files.delete(directory);}
  }
}
