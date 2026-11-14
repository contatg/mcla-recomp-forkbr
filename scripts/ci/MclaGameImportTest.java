package org.mcla.recomp;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Comparator;
import java.util.stream.Stream;

public final class MclaGameImportTest {
    private static final MclaGameImport.Progress NO_PROGRESS = (count, name, mib) -> {};

    public static void main(String[] args) throws IOException {
        Path root = Files.createTempDirectory("mcla-game-import-test");
        try {
            File folder = root.toFile();
            String[] files = MclaGameImport.REQUIRED;
            for (String name : files) {
                Files.write(root.resolve(name), new byte[] {1});
            }
            require(MclaGameImport.isReady(folder), "A complete legacy import must still work");

            // Simulate retrying an old partial copy with a bad first file and stale later files.
            Files.write(root.resolve(files[0]), new byte[0]);
            expectFailure(folder, name -> {
                if (name.equals(files[1])) throw new IOException("Interrupted import");
                return new ByteArrayInputStream(new byte[] {2});
            }, "Interrupted import");
            require(!Files.exists(root.resolve(files[files.length - 1])),
                    "Retry must clear stale files from the old partial copy");

            expectFailure(folder, name -> new ByteArrayInputStream(new byte[0]),
                    "Empty file: " + files[0]);
            require(!Files.exists(root.resolve(files[0] + ".partial")),
                    "Failed file must not leave a partial file behind");

            // An app restart between the final rename and marker removal still requires a retry.
            for (String name : files) {
                Files.write(root.resolve(name), new byte[] {2});
            }
            require(!MclaGameImport.isReady(folder), "Pending import must keep Play disabled");
            MclaGameImport.copy(folder, name -> new ByteArrayInputStream(new byte[] {3}), NO_PROGRESS);
            require(MclaGameImport.isReady(folder), "Successful retry must enable Play");
            for (String name : files) {
                require(Files.readAllBytes(root.resolve(name))[0] == 3,
                        "Successful retry must install a consistent file set");
            }
        } finally {
            try (Stream<Path> paths = Files.walk(root)) {
                for (Path path : (Iterable<Path>) paths.sorted(Comparator.reverseOrder())::iterator) {
                    Files.delete(path);
                }
            }
        }
    }

    private static void expectFailure(File folder, MclaGameImport.Source source, String message)
            throws IOException {
        try {
            MclaGameImport.copy(folder, source, NO_PROGRESS);
            throw new AssertionError("Expected import failure: " + message);
        } catch (IOException error) {
            require(message.equals(error.getMessage()), "Unexpected import failure: " + error);
            require(!MclaGameImport.isReady(folder), "Failed import must keep Play disabled");
        }
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
