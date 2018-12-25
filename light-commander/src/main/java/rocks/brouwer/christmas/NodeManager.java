package rocks.brouwer.christmas;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;

public class NodeManager implements Closeable {
    private static final Logger LOGGER = LoggerFactory.getLogger(NodeManager.class);

    private static final int PORT_CMDR = 1224;
    private static final int PORT_NODE = 1225;

    private static final byte ID_PROG = 12;
    private static final byte ID_NODE = 24;
    private static final byte ID_CMDR = 25;

    private static final byte MSG_PREPARE = 1;
    private static final byte MSG_START = 2;
    private static final byte MSG_STOP = 3;
    private static final byte MSG_DEBUG = 4;

    private final InetAddress broadcastAddress;
    private DatagramSocket cmdrSocket;
    private Map<Byte, Node> nodes = new HashMap<>();

    NodeManager() throws SocketException, UnknownHostException {
        this.broadcastAddress = InetAddress.getByName("192.168.1.255");
        this.cmdrSocket = new DatagramSocket(PORT_CMDR);
        new Thread(this::runServer, "light-commander").start();
    }

    void prepare(byte nodeId, String animationPath) {
        Node node = nodes.get(nodeId);

        if (animationPath != null && node != null) {
            try (ByteArrayOutputStream baos = startMessage(MSG_PREPARE)) {
                baos.write(animationPath.getBytes(StandardCharsets.US_ASCII));
                sendMsg(node, baos.toByteArray());
            } catch (IOException e) {
                LOGGER.error("Cannot prepare node {} for {}", nodeId, animationPath, e);
            }
        } else {
            LOGGER.warn("Nothing playing on node {}", nodeId);
        }
    }

    boolean isPrepared() {
        for (Node node : nodes.values()) {
            if (!node.prepared) return false;
        }
        return true;
    }

    void start() {
        try (ByteArrayOutputStream out = startMessage(MSG_START)) {
            sendMsg(out.toByteArray());
        } catch (IOException e) {
            LOGGER.error("Cannot start animation", e);
        }
    }

    void stop() {
        try (ByteArrayOutputStream baos = startMessage(MSG_STOP)) {
            sendMsg(baos.toByteArray());
        } catch (IOException e) {
            LOGGER.error("Cannot stop animation", e);
        }
    }

    private void runServer() {
        final byte[] buf = new byte[256];

        while (cmdrSocket != null && !cmdrSocket.isClosed()) {
            try {
                final DatagramPacket data = new DatagramPacket(buf, buf.length);
                cmdrSocket.receive(data);

                final int off = data.getOffset();
                final int len = data.getLength();
                LOGGER.debug("Received message from {}, length {}", data.getAddress(), len);

                try (ByteArrayInputStream msg = new ByteArrayInputStream(buf, off, len)) {
                    if (isNodeMsg(msg)) {
                        byte nodeId = (byte) msg.read();
                        handleMsg(toNode(nodeId, data.getAddress()), msg);
                    }
                }
            } catch (IOException ioe) {
                if (!cmdrSocket.isClosed()) {
                    LOGGER.error("Error receiving message", ioe);
                }
            }
        }
    }

    private Node toNode(byte nodeId, InetAddress address) {
        Node node = nodes.get(nodeId);
        if (node == null) {
            node = new Node(nodeId, address);
            nodes.put(nodeId, node);
            LOGGER.info("Found new node {} @ {}", nodeId, address);
        } else if (!node.address.equals(address)) {
            node.address = address;
            LOGGER.warn("Node {} has a new address {}", nodeId, address);
        }
        return node;
    }

    private void handleMsg(Node node, InputStream msg) throws IOException {
        byte msgId = (byte) msg.read();
//        LOGGER.info("Received {} from {}", msgId, node.id);
        switch (msgId) {
            case MSG_DEBUG:
                LOGGER.info("Node {}: {}", node.id, new String(msg.readAllBytes(), StandardCharsets.US_ASCII));
                break;
            case MSG_PREPARE:
                node.prepared = true;
                break;
        }
    }

    private ByteArrayOutputStream startMessage(byte msgId) {
        ByteArrayOutputStream msg = new ByteArrayOutputStream();
        msg.write(ID_PROG);
        msg.write(ID_CMDR);
        msg.write(msgId);
        return msg;
    }

    private void sendMsg(byte[] msg) throws IOException {
        cmdrSocket.send(new DatagramPacket(msg, msg.length, this.broadcastAddress, PORT_NODE));
    }

    private void sendMsg(Node node, byte[] msg) throws IOException {
        cmdrSocket.send(new DatagramPacket(msg, msg.length, node.address, PORT_NODE));
    }

    private static boolean isNodeMsg(InputStream msg) throws IOException {
        if (msg.available() > 2) {
            return ((byte) msg.read()) == ID_PROG && ((byte) msg.read()) == ID_NODE;
        }
        return false;
    }

    public void close() {
        cmdrSocket.close();
    }

    private class Node {
        final byte id;
        InetAddress address;
        boolean prepared;

        Node(byte id, InetAddress address) {
            this.id = id;
            this.address = address;
        }
    }
}
