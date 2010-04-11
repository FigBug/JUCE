/*
  ==============================================================================

   This file is part of the JUCE library - "Jules' Utility Class Extensions"
   Copyright 2004-9 by Raw Material Software Ltd.

  ------------------------------------------------------------------------------

   JUCE can be redistributed and/or modified under the terms of the GNU General
   Public License (Version 2), as published by the Free Software Foundation.
   A copy of the license is included in the JUCE distribution, or can be found
   online at www.gnu.org/licenses.

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.rawmaterialsoftware.com/juce for more information.

  ==============================================================================
*/

/*
    This file contains posix routines that are common to both the Linux and Mac builds.

    It gets included directly in the cpp files for these platforms.
*/


//==============================================================================
CriticalSection::CriticalSection() throw()
{
    pthread_mutexattr_t atts;
    pthread_mutexattr_init (&atts);
    pthread_mutexattr_settype (&atts, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init (&internal, &atts);
}

CriticalSection::~CriticalSection() throw()
{
    pthread_mutex_destroy (&internal);
}

void CriticalSection::enter() const throw()
{
    pthread_mutex_lock (&internal);
}

bool CriticalSection::tryEnter() const throw()
{
    return pthread_mutex_trylock (&internal) == 0;
}

void CriticalSection::exit() const throw()
{
    pthread_mutex_unlock (&internal);
}


//==============================================================================
class WaitableEventImpl
{
public:
    WaitableEventImpl (const bool manualReset_)
        : triggered (false),
          manualReset (manualReset_)
    {
        pthread_cond_init (&condition, 0);
        pthread_mutex_init (&mutex, 0);
    }

    ~WaitableEventImpl()
    {
        pthread_cond_destroy (&condition);
        pthread_mutex_destroy (&mutex);
    }

    bool wait (const int timeOutMillisecs) throw()
    {
        pthread_mutex_lock (&mutex);

        if (! triggered)
        {
            if (timeOutMillisecs < 0)
            {
                do
                {
                    pthread_cond_wait (&condition, &mutex);
                }
                while (! triggered);
            }
            else
            {
                struct timeval now;
                gettimeofday (&now, 0);

                struct timespec time;
                time.tv_sec  = now.tv_sec  + (timeOutMillisecs / 1000);
                time.tv_nsec = (now.tv_usec + ((timeOutMillisecs % 1000) * 1000)) * 1000;

                if (time.tv_nsec >= 1000000000)
                {
                    time.tv_nsec -= 1000000000;
                    time.tv_sec++;
                }

                do
                {
                    if (pthread_cond_timedwait (&condition, &mutex, &time) == ETIMEDOUT)
                    {
                        pthread_mutex_unlock (&mutex);
                        return false;
                    }
                }
                while (! triggered);
            }
        }

        if (! manualReset)
            triggered = false;

        pthread_mutex_unlock (&mutex);
        return true;
    }

    void signal() throw()
    {
        pthread_mutex_lock (&mutex);
        triggered = true;
        pthread_cond_broadcast (&condition);
        pthread_mutex_unlock (&mutex);
    }

    void reset() throw()
    {
        pthread_mutex_lock (&mutex);
        triggered = false;
        pthread_mutex_unlock (&mutex);
    }

private:
    pthread_cond_t condition;
    pthread_mutex_t mutex;
    bool triggered;
    const bool manualReset;

    WaitableEventImpl (const WaitableEventImpl&);
    WaitableEventImpl& operator= (const WaitableEventImpl&);
};

WaitableEvent::WaitableEvent (const bool manualReset) throw()
    : internal (new WaitableEventImpl (manualReset))
{
}

WaitableEvent::~WaitableEvent() throw()
{
    delete static_cast <WaitableEventImpl*> (internal);
}

bool WaitableEvent::wait (const int timeOutMillisecs) const throw()
{
    return static_cast <WaitableEventImpl*> (internal)->wait (timeOutMillisecs);
}

void WaitableEvent::signal() const throw()
{
    static_cast <WaitableEventImpl*> (internal)->signal();
}

void WaitableEvent::reset() const throw()
{
    static_cast <WaitableEventImpl*> (internal)->reset();
}

//==============================================================================
void JUCE_CALLTYPE Thread::sleep (int millisecs)
{
    struct timespec time;
    time.tv_sec = millisecs / 1000;
    time.tv_nsec = (millisecs % 1000) * 1000000;
    nanosleep (&time, 0);
}


//==============================================================================
const juce_wchar File::separator = '/';
const juce_wchar* File::separatorString = L"/";

//==============================================================================
const File File::getCurrentWorkingDirectory()
{
    HeapBlock<char> heapBuffer;

    char localBuffer [1024];
    char* cwd = getcwd (localBuffer, sizeof (localBuffer) - 1);
    int bufferSize = 4096;

    while (cwd == 0 && errno == ERANGE)
    {
        heapBuffer.malloc (bufferSize);
        cwd = getcwd (heapBuffer, bufferSize - 1);
        bufferSize += 1024;
    }

    return File (String::fromUTF8 (cwd));
}

bool File::setAsCurrentWorkingDirectory() const
{
    return chdir (getFullPathName().toUTF8()) == 0;
}

//==============================================================================
bool juce_copyFile (const String& s, const String& d);

static bool juce_stat (const String& fileName, struct stat& info)
{
    return fileName.isNotEmpty()
            && (stat (fileName.toUTF8(), &info) == 0);
}

bool juce_isDirectory (const String& fileName)
{
    struct stat info;

    return fileName.isEmpty()
            || (juce_stat (fileName, info)
                  && ((info.st_mode & S_IFDIR) != 0));
}

bool juce_fileExists (const String& fileName, const bool dontCountDirectories)
{
    if (fileName.isEmpty())
        return false;

    const char* const fileNameUTF8 = fileName.toUTF8();
    bool exists = access (fileNameUTF8, F_OK) == 0;

    if (exists && dontCountDirectories)
    {
        struct stat info;
        const int res = stat (fileNameUTF8, &info);

        if (res == 0 && (info.st_mode & S_IFDIR) != 0)
            exists = false;
    }

    return exists;
}

int64 juce_getFileSize (const String& fileName)
{
    struct stat info;
    return juce_stat (fileName, info) ? info.st_size : 0;
}

//==============================================================================
bool juce_canWriteToFile (const String& fileName)
{
    return access (fileName.toUTF8(), W_OK) == 0;
}

bool juce_deleteFile (const String& fileName)
{
    if (juce_isDirectory (fileName))
        return rmdir (fileName.toUTF8()) == 0;
    else
        return remove (fileName.toUTF8()) == 0;
}

bool juce_moveFile (const String& source, const String& dest)
{
    if (rename (source.toUTF8(), dest.toUTF8()) == 0)
        return true;

    if (juce_canWriteToFile (source)
         && juce_copyFile (source, dest))
    {
        if (juce_deleteFile (source))
            return true;

        juce_deleteFile (dest);
    }

    return false;
}

void juce_createDirectory (const String& fileName)
{
    mkdir (fileName.toUTF8(), 0777);
}

void* juce_fileOpen (const String& fileName, bool forWriting)
{
    int flags = O_RDONLY;

    if (forWriting)
    {
        if (juce_fileExists (fileName, false))
        {
            const int f = open (fileName.toUTF8(), O_RDWR, 00644);

            if (f != -1)
                lseek (f, 0, SEEK_END);

            return (void*) f;
        }
        else
        {
            flags = O_RDWR + O_CREAT;
        }
    }

    return (void*) open (fileName.toUTF8(), flags, 00644);
}

void juce_fileClose (void* handle)
{
    if (handle != 0)
        close ((int) (pointer_sized_int) handle);
}

int juce_fileRead (void* handle, void* buffer, int size)
{
    if (handle != 0)
        return jmax (0, (int) read ((int) (pointer_sized_int) handle, buffer, size));

    return 0;
}

int juce_fileWrite (void* handle, const void* buffer, int size)
{
    if (handle != 0)
        return (int) write ((int) (pointer_sized_int) handle, buffer, size);

    return 0;
}

int64 juce_fileSetPosition (void* handle, int64 pos)
{
    if (handle != 0 && lseek ((int) (pointer_sized_int) handle, pos, SEEK_SET) == pos)
        return pos;

    return -1;
}

int64 juce_fileGetPosition (void* handle)
{
    if (handle != 0)
        return lseek ((int) (pointer_sized_int) handle, 0, SEEK_CUR);

    return -1;
}

void juce_fileFlush (void* handle)
{
    if (handle != 0)
        fsync ((int) (pointer_sized_int) handle);
}

const File juce_getExecutableFile()
{
    Dl_info exeInfo;
    dladdr ((const void*) juce_getExecutableFile, &exeInfo);
    return File::getCurrentWorkingDirectory().getChildFile (String::fromUTF8 (exeInfo.dli_fname));
}

//==============================================================================
// if this file doesn't exist, find a parent of it that does..
static bool doStatFS (const File* file, struct statfs& result)
{
    File f (*file);

    for (int i = 5; --i >= 0;)
    {
        if (f.exists())
            break;

        f = f.getParentDirectory();
    }

    return statfs (f.getFullPathName().toUTF8(), &result) == 0;
}

int64 File::getBytesFreeOnVolume() const
{
    struct statfs buf;
    if (doStatFS (this, buf))
        return (int64) buf.f_bsize * (int64) buf.f_bavail; // Note: this returns space available to non-super user

    return 0;
}

int64 File::getVolumeTotalSize() const
{
    struct statfs buf;
    if (doStatFS (this, buf))
        return (int64) buf.f_bsize * (int64) buf.f_blocks;

    return 0;
}

const String juce_getVolumeLabel (const String& filenameOnVolume,
                                  int& volumeSerialNumber)
{
    volumeSerialNumber = 0;

#if JUCE_MAC
    struct VolAttrBuf
    {
        u_int32_t       length;
        attrreference_t mountPointRef;
        char            mountPointSpace [MAXPATHLEN];
    } attrBuf;

    struct attrlist attrList;
    zerostruct (attrList);
    attrList.bitmapcount = ATTR_BIT_MAP_COUNT;
    attrList.volattr = ATTR_VOL_INFO | ATTR_VOL_NAME;

    File f (filenameOnVolume);

    for (;;)
    {
        if (getattrlist (f.getFullPathName().toUTF8(),
                         &attrList, &attrBuf, sizeof(attrBuf), 0) == 0)
        {
            return String::fromUTF8 (((const char*) &attrBuf.mountPointRef) + attrBuf.mountPointRef.attr_dataoffset,
                                     (int) attrBuf.mountPointRef.attr_length);
        }

        const File parent (f.getParentDirectory());

        if (f == parent)
            break;

        f = parent;
    }
#endif

    return String::empty;
}


//==============================================================================
void juce_runSystemCommand (const String& command)
{
    int result = system (command.toUTF8());
    (void) result;
}

const String juce_getOutputFromCommand (const String& command)
{
    // slight bodge here, as we just pipe the output into a temp file and read it...
    const File tempFile (File::getSpecialLocation (File::tempDirectory)
                           .getNonexistentChildFile (String::toHexString (Random::getSystemRandom().nextInt()), ".tmp", false));

    juce_runSystemCommand (command + " > " + tempFile.getFullPathName());

    String result (tempFile.loadFileAsString());
    tempFile.deleteFile();
    return result;
}


//==============================================================================
class InterProcessLock::Pimpl
{
public:
    Pimpl (const String& name, const int timeOutMillisecs)
        : handle (0), refCount (1)
    {
#if JUCE_MAC
        // (don't use getSpecialLocation() to avoid the temp folder being different for each app)
        const File temp (File ("~/Library/Caches/Juce").getChildFile (name));
#else
        const File temp (File::getSpecialLocation (File::tempDirectory).getChildFile (name));
#endif
        temp.create();
        handle = open (temp.getFullPathName().toUTF8(), O_RDWR);

        if (handle != 0)
        {
            struct flock fl;
            zerostruct (fl);
            fl.l_whence = SEEK_SET;
            fl.l_type = F_WRLCK;

            const int64 endTime = Time::currentTimeMillis() + timeOutMillisecs;

            for (;;)
            {
                const int result = fcntl (handle, F_SETLK, &fl);

                if (result >= 0)
                    return;

                if (errno != EINTR)
                {
                    if (timeOutMillisecs == 0
                         || (timeOutMillisecs > 0 && Time::currentTimeMillis() >= endTime))
                        break;

                    Thread::sleep (10);
                }
            }
        }

        closeFile();
    }

    ~Pimpl()
    {
        closeFile();
    }

    void closeFile()
    {
        if (handle != 0)
        {
            struct flock fl;
            zerostruct (fl);
            fl.l_whence = SEEK_SET;
            fl.l_type = F_UNLCK;

            while (! (fcntl (handle, F_SETLKW, &fl) >= 0 || errno != EINTR))
            {}

            close (handle);
            handle = 0;
        }
    }

    int handle, refCount;
};

InterProcessLock::InterProcessLock (const String& name_)
    : name (name_)
{
}

InterProcessLock::~InterProcessLock()
{
}

bool InterProcessLock::enter (const int timeOutMillisecs)
{
    const ScopedLock sl (lock);

    if (pimpl == 0)
    {
        pimpl = new Pimpl (name, timeOutMillisecs);

        if (pimpl->handle == 0)
            pimpl = 0;
    }
    else
    {
        pimpl->refCount++;
    }

    return pimpl != 0;
}

void InterProcessLock::exit()
{
    const ScopedLock sl (lock);

    if (pimpl != 0 && --(pimpl->refCount) == 0)
        pimpl = 0;
}
