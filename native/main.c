// copyright 2017 nqzero - see License.txt for terms

#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fcntl.h>
#include <jni.h>
#include <string.h>
#include <errno.h>

#define DIO_PACKAGE(name) Java_com_nqzero_directio_DioNative_##name


int getAdvice(jint jadvice) {
    switch (jadvice) {
        case 0: return POSIX_FADV_NORMAL;
        case 1: return POSIX_FADV_SEQUENTIAL;
        case 2: return POSIX_FADV_RANDOM;
        case 3: return POSIX_FADV_WILLNEED;
        case 4: return POSIX_FADV_DONTNEED;
        case 5: return POSIX_FADV_NOREUSE;
    }
    return POSIX_FADV_NORMAL;
}

jint doReturn(JNIEnv *env,int result) {
    if (result != 0) {
        jclass ioex = (*env)->FindClass(env, "java/io/IOException");
        (*env)->ThrowNew(env, ioex, strerror(errno));
        return -1;
    }
    else return 0;
}

/*
 * Class:     DIO_PACKAGE
 * Method:    fadvises
 * Signature: (I[JJI)I
 */
JNIEXPORT jint JNICALL DIO_PACKAGE(fadvises)
        (JNIEnv *env, jclass _ignore, jint fd, jlongArray joffsets, jlong len, jint jadvice) {

    int advice = getAdvice( jadvice );
    int result = 1;
    jsize nn = (*env)->GetArrayLength( env, joffsets );
    jlong * offsets = (*env)->GetLongArrayElements( env, joffsets, NULL ); // read-only
    for (int ii = 0; ii < nn; ii++)
        result &= posix_fadvise(fd, offsets[ii], len, advice);
    (*env)->ReleaseLongArrayElements( env, joffsets, offsets, JNI_ABORT ); // no changes
    return doReturn( env, result );
}


/*
 * Class:     DIO_PACKAGE
 * Method:    fadvise
 * Signature: (IJJI)I
 */
JNIEXPORT jint JNICALL DIO_PACKAGE(fadvise)
        (JNIEnv *env, jclass _ignore, jint fd, jlong offset, jlong len, jint jadvice) {

    int advice = getAdvice( jadvice );
    int result = posix_fadvise(fd, offset, len, advice);
    return doReturn( env, result );
}

/*
 * Class:     DIO_PACKAGE
 * Method:    fallocate
 * Signature: (IJJI)I
 */
JNIEXPORT jint JNICALL DIO_PACKAGE(fallocate)
        (JNIEnv *env, jclass _ignore, jint fd, jlong offset, jlong len) {

    int result = posix_fallocate(fd, offset, len);
    return doReturn( env, result );
}

typedef struct {
    char *name;
    int fd;
    int skip;
    int bs;
    int niter;
    unsigned char *buffer, *bufbase; // the aligned (unfreeable) buffer, and the true buffer
    int sum;
    long fileSize;
    int offset;
    int fadv, drop;
} reader;

reader* reader_open(reader *xx,int flags) {
    xx->fd = open( xx->name, flags );
    if (xx->fd < 0) {
        perror( xx->name );
        exit(EXIT_FAILURE);
    }
    xx->fileSize = lseek( xx->fd, 0, SEEK_END );
    lseek( xx->fd, 0, SEEK_SET );
    return xx;
}

reader* reader_init(char *name,int bs,int skip,int niter,int drop,int fadv) {
    reader *xx = calloc( 1, sizeof( reader ) );
    xx->name = name;
    xx->bs = bs;
    xx->skip = skip;
    xx->niter = niter;
    int ps = getpagesize();
    xx->bufbase = malloc( bs + ps );
    xx->buffer = (unsigned char *) (((unsigned long)(xx->bufbase) + ps - 1)/ps*ps);
    xx->fadv = fadv;
    xx->drop = drop;
    return xx;
}

char *fmt = "\
reader:\n\
\tname:%s\n\
\tfd:%d\n\
\tbs:%d\n\
\tskip:%d\n\
\tniter:%d\n\
\tsum:%d\n\
\tfileSize:%ld\n\
\toffset:%d\n\
\tfadv:%d\n\
\tdrop:%d\n\
";

void reader_format(reader *xx) {
    printf( fmt, xx->name, xx->fd, xx->bs, xx->skip, xx->niter, xx->sum, xx->fileSize, xx->offset, xx->fadv, xx->drop );
}

int nocache(reader *xx) {
    int skipThresh = 16;

    int bs = xx->bs;
    int skip = xx->skip;
    long total = xx->fileSize / bs * bs;
    long bpf = xx->fileSize / bs;

    // the data buffer needs to be aligned to use DirectIO ... not using it anymore
    //   leaving it here since it doesn't hurt anything and i might want to experiment with dio again ... srl@2010.12.20
    int ps = getpagesize();
    unsigned char* rbuf = malloc( bs + ps );
    unsigned char* data = (unsigned char *) (((unsigned long)rbuf + ps - 1)/ps*ps);

    long sum = 0;

    long kblock0 = xx->offset;
    long niter = xx->niter;
    int ns = niter / 16;

    if ((skip > skipThresh) && xx->fadv) {
        printf( "using fadvise -- skip:%d, %d\n", skip, xx->fadv );
        posix_fadvise( xx->fd, 0, total, POSIX_FADV_RANDOM );
        for (int ii = 0; ii < niter; ii++) {
            long kblock = (kblock0 + ii * skip) % bpf;
            long addr = kblock * bs;
            posix_fadvise( xx->fd, addr, bs, POSIX_FADV_WILLNEED );
        }
    }
    int error = 0, jj = 0;
    for (int ii = 0; ii < niter; ii++) {
        long kblock = (kblock0 + ii * skip) % bpf;
        long addr = kblock * bs;
        lseek( xx->fd, addr, SEEK_SET );
        int nread = read( xx->fd, data, bs );
        if (nread < bs) { perror("reading"); error = 1; break; }
        int val = data[ jj ];
        sum += val;
        if (ii%ns==0) printf( "%5d: %8ld --> %5d, %5d --> %5d, %8ld\n", ii, addr, nread, jj, val, sum );
        jj += (val==0) ? 2 : 1;
        jj %= bs;
        if (xx->fadv && xx->drop) posix_fadvise( xx->fd, addr, bs, POSIX_FADV_DONTNEED );
    }
    if (xx->drop) posix_fadvise( xx->fd, 0, total, POSIX_FADV_DONTNEED );

    if (close(xx->fd) < 0) {
        perror("closing a file");
        exit(EXIT_FAILURE);
    }

    long meg = 1024L*1024;
    printf( "running sum: %8ld, filesize: %ldMB, total: %ldMB, skip: %d, skips: %5ld\n", sum, total/meg, 1L*niter*skip*bs/meg, skip, niter );

    return (error ? -1 : sum & 0x08fffffff);
}

JNIEXPORT jint JNICALL DIO_PACKAGE(readify)
    (JNIEnv *env, jclass _ignore, jint bs,jint skip,jint niter,jint drop,jint fadv) {
    reader *xx = reader_init( "/data/raw/lytles/t2_0.mmap", bs, skip, niter, drop, fadv );
    reader_open( xx, O_RDWR );
    int ret = nocache( xx );
    close( xx->fd );
    return ret;
}


int main(int argc, char** argv) {
    
    reader *xx;
    xx = reader_init( "/data/raw/lytles/t2_0.mmap", 1<<12, 512, 1024, 0, 0 );
    // xx = reader_init( "/data/raw/lytles/t2_0.mmap", 1<<12, 1, 1<<19, 1, 0 );
    int error = 0, ret = -1;
    for (int ii = 1; ii < argc && error == 0; ii++) {
        char * arg = argv[ii];
        printf( "arg: %s\n", arg );
        char * cmd = malloc( strlen( arg ) ); cmd[0] = 0;
        char * rem = malloc( strlen( arg ) ); rem[0] = 0;
        sscanf( arg, "%[^=]=%s", cmd, rem );
        long val = 0;
        int isnum = sscanf( rem, "%ld", &val );
        if      (! strcmp( cmd, "drop"  )) xx->drop = 1;
        else if (! strcmp( cmd, "fadv"  )) xx->fadv = 1;
        else if (! strcmp( cmd, "skip"  )) xx->skip = val;
        else if (! strcmp( cmd, "niter" )) xx->niter = val;
        else if (! strcmp( cmd, "bs"    )) xx->bs = val;
        else if (! strcmp( cmd, "name"  )) xx->name = rem;
        else {
            printf( "invalid arg: %s --> %s, %ld\n", arg, cmd, val );
            error = 1;
        }
        free( cmd );
        free( rem );
    }
    reader_format( xx );
    if (error == 0) {
        reader_open( xx, O_RDWR );
        ret = nocache( xx );
        close( xx->fd );
    }
    return ret==-1 ? EXIT_FAILURE : EXIT_SUCCESS;
}

