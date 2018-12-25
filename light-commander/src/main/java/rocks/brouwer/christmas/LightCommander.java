package rocks.brouwer.christmas;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.sound.sampled.LineUnavailableException;
import javax.sound.sampled.UnsupportedAudioFileException;
import java.io.*;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;
import java.util.Scanner;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.ScheduledThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

public class LightCommander implements Closeable {
    private static final Logger LOGGER = LoggerFactory.getLogger(LightCommander.class);

    private Scanner prompt = new Scanner(System.in);
    private AudioPlayer audio;
    private NodeManager nodes;
    private ScheduledThreadPoolExecutor scheduler = new ScheduledThreadPoolExecutor(1);
    private ScheduledFuture<?> nodesPlay;
    private Map<String, Sequence> sequences = new HashMap<>();
    private float preparedAudioStart = 0;

    public static void main(String... args) throws IOException, UnsupportedAudioFileException {
        try (LightCommander cmdr = new LightCommander()) {
            while (cmdr.execCommand() >= 0) {
                // ask for the next command
            }
        }
    }

    private LightCommander() throws IOException, UnsupportedAudioFileException {
        this.audio = new AudioPlayer(new File("audio.wav"));
        this.nodes = new NodeManager();
        readSequences(new File("sequence.csv"));
    }

    private void readSequences(File file) throws IOException {
        try (BufferedReader reader = new BufferedReader(new FileReader(file))) {
            String line = null;
            while ((line = reader.readLine()) != null) {
                final String[] parts = line.split(",");
                if (parts.length > 2) {
                    sequences.put(parts[0], new Sequence(parts[0], Float.parseFloat(parts[1]), Arrays.copyOfRange(parts, 2, parts.length)));
                }
            }
        }
    }

    private int execCommand() {
        System.out.println("Command: ");

        final String command = prompt.next();
        if ("quit".equalsIgnoreCase(command) || "exit".equalsIgnoreCase(command)) {
            return -1;
        } else if ("stop".equalsIgnoreCase(command)) {
            stop();
            return 1;
        } else if (prepare(command)) {
            return 2;
        }
        return 0;
    }

    private void stop() {
        System.out.println("Stopping");
        audio.stop();
        nodes.stop();
    }

    private boolean prepare(String id) {
        Sequence seq = sequences.get(id);
        if (seq == null) {
            LOGGER.error("No sequence with id '{}' found", id);
            return false;
        }
        this.preparedAudioStart = seq.audioStart;
        for (int i = 0; i < seq.animations.length; i++) {
            nodes.prepare((byte) (i + 1), seq.animations[i]);
        }
        this.nodesPlay = scheduler.schedule(this::play, 1, TimeUnit.SECONDS);
        return true;
    }

    private void play() {
        if (!nodes.isPrepared()) {
            LOGGER.warn("Not all nodes prepared");
        }
        nodes.start();

        try {
            audio.play(preparedAudioStart - 5.0f);
        } catch (Exception e) {
            LOGGER.error("Cannot play audio", e);
        }
    }

    @Override
    public void close() throws IOException {
        this.scheduler.shutdown();
        if (this.nodesPlay != null) {
            this.nodesPlay.cancel(false);
        }
        try {
            audio.close();
        } finally {
            nodes.close();
        }
    }

    private class Sequence {
        final String id;
        final float audioStart;
        final String[] animations;

        Sequence(String id, float audioStart, String[] animations) {
            this.id = id;
            this.audioStart = audioStart;
            this.animations = animations;
        }
    }
}
