package rocks.brouwer.christmas;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.sound.sampled.*;
import java.io.Closeable;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;


public class AudioPlayer implements Closeable {
    private static final Logger LOGGER = LoggerFactory.getLogger(AudioPlayer.class);

    private final File file;

    private AudioInputStream stream;
    private AudioFormat format;
    private SourceDataLine line;
    private Thread playThread;

    private volatile boolean stopFlag = false;
    private volatile boolean playing = false;

    public AudioPlayer(File file) throws IOException, UnsupportedAudioFileException {
        this.file = file;
        final AudioInputStream in = AudioSystem.getAudioInputStream(file);
        final AudioFormat baseFormat = in.getFormat();
        this.format = new AudioFormat(AudioFormat.Encoding.PCM_SIGNED,
                baseFormat.getSampleRate(),
                16,
                baseFormat.getChannels(),
                baseFormat.getChannels() * 2,
                baseFormat.getSampleRate(),
                false);
        this.stream = AudioSystem.getAudioInputStream(format, in);
        this.stream.mark(50_000_000);
    }

    /**
     * @throws IOException
     * @throws UnsupportedAudioFileException
     * @throws FileNotFoundException
     * @throws LineUnavailableException
     */
    void playAudio() {
        final byte[] data = new byte[4096];
        try {
            int bytesRead = 0;// nBytesWritten = 0;
            while ((bytesRead != -1) && (!stopFlag)) {
                bytesRead = stream.read(data, 0, data.length);
                if (bytesRead != -1) {
                    line.write(data, 0, bytesRead);
                }
            }
            //System.out.println("Done ...");

            // Stop
            line.drain();
            line.stop();
            line.close();
            line = null;
            this.playing = false;
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    public void play(float start) throws IOException, LineUnavailableException, UnsupportedAudioFileException {
        this.playing = true;
        this.stream.reset();
        long audioFramesToSkip = (long) (format.getFrameRate() * start);
        long bytesToSkip = format.getFrameSize() * audioFramesToSkip;
        long skipped = stream.skip(bytesToSkip);
        LOGGER.debug("Asked to skip {} bytes, actually skipped {} bytes", bytesToSkip, skipped);

        final DataLine.Info info = new DataLine.Info(SourceDataLine.class, format);
        SourceDataLine line = (SourceDataLine) AudioSystem.getLine(info);
        line.open(format);
        this.line = line;
        this.line.start();

        this.stopFlag = false;
        this.playThread = new Thread(this::playAudio, "media-player");
        this.playThread.start();
    }

    public void stop() {
        this.stopFlag = true;
    }

    public void close() throws IOException {
        stop();
        stream.close();
    }
}