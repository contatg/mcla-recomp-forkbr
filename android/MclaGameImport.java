package org.mcla.recomp;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

final class MclaGameImport {
    static final String[] REQUIRED = {
        "default.xex", "xarchive_audio.rpf", "xarchive_audlo.rpf",
        "xarchive_cache.rpf", "xarchive_music.rpf",
        "attract576_16x9.bik", "attract576_4x3.bik", "attract720.bik",
        "intro576_16x9.bik", "intro576_4x3.bik", "intro720.bik",
        "nxeart", "root_directory_padding.pad"
    };
    private static final String IN_PROGRESS = ".import-in-progress";
    private static final long MIB = 1024L * 1024;

    interface Source {
        InputStream open(String name) throws IOException;
    }

    interface Progress {
        void update(int completed, String name, long copiedMiB);
    }

    static boolean isReady(File folder) {
        if (new File(folder, IN_PROGRESS).exists()) return false;
        for (String name : REQUIRED) {
            File file = new File(folder, name);
            if (!file.isFile() || file.length() == 0) return false;
        }
        return true;
    }

    static synchronized void copy(File folder, Source source, Progress progress) throws IOException {
        if (isReady(folder)) return;
        if (!folder.isDirectory() && !folder.mkdirs()) {
            throw new IOException("Could not create the private game folder.");
        }

        // Keep Play disabled across process restarts until the entire copy succeeds.
        File marker = new File(folder, IN_PROGRESS);
        marker.createNewFile();
        for (String name : REQUIRED) {
            deleteIfPresent(new File(folder, name + ".partial"));
            deleteIfPresent(new File(folder, name));
        }

        byte[] buffer = new byte[(int) MIB];
        for (int index = 0; index < REQUIRED.length; index++) {
            String name = REQUIRED[index];
            progress.update(index, name, 0);
            File partial = new File(folder, name + ".partial");
            try {
                try (InputStream input = source.open(name)) {
                    if (input == null) throw new IOException("Could not open " + name);
                    try (FileOutputStream output = new FileOutputStream(partial)) {
                        long copied = 0;
                        long nextReport = 64 * MIB;
                        int count;
                        while ((count = input.read(buffer)) != -1) {
                            output.write(buffer, 0, count);
                            copied += count;
                            if (copied >= nextReport) {
                                progress.update(index, name, copied / MIB);
                                nextReport += 64 * MIB;
                            }
                        }
                        if (copied == 0) throw new IOException("Empty file: " + name);
                        output.getFD().sync();
                    }
                }
                if (!partial.renameTo(new File(folder, name))) {
                    throw new IOException("Could not save " + name);
                }
            } finally {
                partial.delete();
            }
        }
        deleteIfPresent(marker);
    }

    private static void deleteIfPresent(File file) throws IOException {
        if (file.exists() && !file.delete()) {
            throw new IOException("Could not remove " + file.getName());
        }
    }

    private MclaGameImport() {}
}
