// copyright 2017 nqzero - see License.txt for terms

package com.nqzero.directio;

import java.io.FileDescriptor;
import java.io.IOException;
import java.lang.reflect.Field;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.NoSuchElementException;
import java.util.Optional;
import java.util.stream.StreamSupport;


public class DioNative {
    private static boolean skip2 = true;
    static {
        String libpath = "";
        try {
            String srcpath = DioNative.class.getProtectionDomain().getCodeSource().getLocation().getPath();
            libpath = Paths.get(srcpath).resolveSibling("directio.so").toString();
            Path parent = Paths.get(srcpath).getParent();
            boolean load = true;
            Optional<Path> cio
                    = StreamSupport.stream(
                            Files.newDirectoryStream(parent,"directio*.so").spliterator(),false)
                    .findFirst();
            // support running from target/classes, ie maven without doing package first (netbeans)
            //   mvn dependency:copy-dependencies -DoutputDirectory=libs
            if (!cio.isPresent()) cio = StreamSupport.stream(
                        Files.newDirectoryStream(parent.resolve("../libs"),"directio*.so").spliterator(),false)
                        .findFirst();
            libpath = cio.isPresent() ? cio.get().toString() : libpath;
            System.out.println( "glob: " + cio.isPresent());
            System.out.println( "loading cio: " + libpath);
            try {
                if (load) System.load(cio.get().toString());
            } catch (NoSuchElementException ex) {
            }
//            if (load) System.load(libpath );
    //        System.loadLibrary( "cio" );
            System.out.println( "loaded cio" );
            skip2 = false;
        }
        catch (java.lang.UnsatisfiedLinkError | Exception ex) {
            System.out.println("DirectIO - failed to load libcio, exception:");
            System.out.println("--------------------------------");
            System.out.println(ex);
            System.out.println("--------------------------------");
            System.out.println("path: " + libpath);
            System.out.println("DirectIO - program will run but on linux performance and cache may be sub-optimal");
            System.out.println();
            System.out.println();
        }
    }

    public static final boolean skip = skip2;
    public enum Enum {
        normal, seq, random, willneed, dontneed, noreuse;
    }

    public static void forceInitialization() {}
    
    
    
    
    
    public static void dropCache() {
        String script = "drop.sh";
        try {
            Runtime.getRuntime().exec( script ).waitFor();
        } catch (Exception ex) {
            throw new RuntimeException( "call to 'drop.sh' failed" );
        }
    }
    public static int systemFD(FileDescriptor jd) {
        try {
            Field field = FileDescriptor.class.getDeclaredField( "fd" );
            field.setAccessible( true );
            int fd = (Integer) field.get( jd );
            return fd;
        } catch (Exception ex) {
            throw new RuntimeException( "couldn't get unix fd from java FileDescriptor", ex );
        }
    }

    // javah -classpath Soup/dist/Soup.jar nqzero.soup.DirectIO

    
    public static int fadvise(int fd, long offset, long len, Enum advise) throws IOException {
        return skip ? -1 : DioNative.fadvise( fd, offset, len, advise.ordinal() );
    }
    public static int fadvise(int fd, long [] offsets, long len, Enum advise) throws IOException {
        return DioNative.fadvises( fd, offsets, len, advise.ordinal() );
    }
    
    public static native Object blah();

    public static native int fallocate(int fd, long offset,     long len            ) throws IOException;
    public static native int fadvise  (int fd, long offset,     long len, int advise) throws IOException;
    public static native int fadvises (int fd, long [] offsets, long len, int advise) throws IOException;

    public static native int readify  (int bs,int skip,int niter,int drop,int fadv  ) throws IOException;

    public static void main(String [] args) throws Exception {
        int fd = systemFD(new java.io.RandomAccessFile("/etc/hosts","r").getFD());
        if (!skip) DioNative.fadvise( fd, 0L, 4096L, 0 );
    }
    
}
