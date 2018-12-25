package rocks.brouwer.christmas;

import org.w3c.dom.NamedNodeMap;
import org.w3c.dom.Node;
import org.w3c.dom.NodeList;

import javax.imageio.ImageIO;
import javax.imageio.ImageReader;
import javax.imageio.metadata.IIOMetadata;
import javax.imageio.stream.ImageInputStream;
import java.awt.*;
import java.awt.image.BufferedImage;
import java.io.*;
import java.util.Arrays;

public class AnimatedGifGlediator {
    public static void main(String... args) {
        try (BufferedOutputStream out = new BufferedOutputStream(new FileOutputStream(args[0]))){

            for (int srcIndex = 1; srcIndex < args.length; srcIndex++) {
                // assumes all images are the same dimensions
                appendFile(args[srcIndex], out);
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    private static void appendFile(String filename, BufferedOutputStream out) throws IOException {
        final ImageReader reader = (ImageReader) ImageIO.getImageReadersByFormatName("gif").next();

        try (final ImageInputStream ciis = ImageIO.createImageInputStream(new File(filename))) {
            reader.setInput(ciis, false);

            final int frames = reader.getNumImages(true);

            int width = 0;
            int height = 0;
            int[] rgbArray = null;
            BufferedImage master = null;

            for (int i = 0; i < frames; i++) {
                final BufferedImage image = reader.read(i);
                final IIOMetadata metadata = reader.getImageMetadata(i);

                final Node tree = metadata.getAsTree("javax_imageio_gif_image_1.0");
                final NodeList children = tree.getChildNodes();

                for (int j = 0; j < children.getLength(); j++) {
                    Node nodeItem = children.item(j);

                    if (nodeItem.getNodeName().equals("ImageDescriptor")) {
                        NamedNodeMap attributes = nodeItem.getAttributes();

                        if (i == 0) {
                            width = Integer.valueOf(attributes.getNamedItem("imageWidth").getNodeValue());
                            height = Integer.valueOf(attributes.getNamedItem("imageHeight").getNodeValue());
                            master = new BufferedImage(width, height, BufferedImage.TYPE_INT_ARGB);
                            rgbArray = new int[width * height];
                        }

                        final int left = Integer.valueOf(attributes.getNamedItem("imageLeftPosition").getNodeValue());
                        final int top = Integer.valueOf(attributes.getNamedItem("imageTopPosition").getNodeValue());

                        master.getGraphics().drawImage(image, left, top, null);
                    }
                }

                appendFrame(out, master.getRGB(0, 0, width, height, rgbArray, 0, width));
            }
        }
    }

    private static void appendFrame(BufferedOutputStream out, int[] pixels) throws IOException {
        for (int pixel : pixels) {
            Color color = new Color(pixel);
            out.write(color.getRed());
            out.write(color.getGreen());
            out.write(color.getBlue());
        }
    }
}
