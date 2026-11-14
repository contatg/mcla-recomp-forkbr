package org.mcla.recomp;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Arrays;

final class MclaLogExporter {
    private static final String TAG = "MCLA Logs";
    private static final String RELATIVE_PATH = Environment.DIRECTORY_DOCUMENTS + "/MCLA/logs/";
    private static final Object LOCK = new Object();

    private MclaLogExporter() {}

    static void saveDiagnostic(Context context, Uri target, Runnable done,
            java.util.function.Consumer<Exception> failed) {
        Context app = context.getApplicationContext();
        new Thread(() -> {
            try {
                try (OutputStream output = app.getContentResolver().openOutputStream(target, "wt")) {
                    if (output == null) throw new java.io.IOException("Cannot open diagnostic file");
                    writeDiagnostic(app, output);
                }
                done.run();
            } catch (Exception error) {
                failed.accept(error);
            }
        }, "MCLA diagnostic").start();
    }

    private static void writeDiagnostic(Context app, OutputStream output) throws Exception {
        String version = app.getPackageManager().getPackageInfo(app.getPackageName(), 0).versionName;
        writeText(output, "MCLA " + version + " | Android " + Build.VERSION.RELEASE
                + " | " + Build.MANUFACTURER + " " + Build.MODEL + "\nCollected "
                + new java.util.Date() + "\nGraphics profile: " + MclaPerformanceProfile.resolve(app)
                + "\n");
        File files = app.getFilesDir();
        for (String path : new String[]{"gpu-drivers/selected.txt", "logs/mcla.1.log", "logs/mcla.log"}) {
            File file = new File(files, path);
            writeText(output, "\n=== " + path + " ===\n");
            if (!file.isFile()) {
                writeText(output, "Unavailable\n");
                continue;
            }
            try (java.io.RandomAccessFile input = new java.io.RandomAccessFile(file, "r")) {
                long length = input.length();
                long remaining = Math.min(length, 2L * 1024 * 1024);
                input.seek(length - remaining);
                byte[] buffer = new byte[8192];
                while (remaining > 0) {
                    int count = input.read(buffer, 0, (int) Math.min(buffer.length, remaining));
                    if (count < 0) break;
                    output.write(buffer, 0, count);
                    remaining -= count;
                }
            } catch (java.io.IOException error) {
                writeText(output, "\nFile changed or could not be read: " + error + "\n");
            }
        }
        if (Build.VERSION.SDK_INT >= 30) {
            writeText(output, "\n=== Android process exits (historical) ===\n");
            ActivityManager manager = app.getSystemService(ActivityManager.class);
            if (manager != null) {
                for (ApplicationExitInfo exit : manager.getHistoricalProcessExitReasons(null, 0, 5)) {
                    writeText(output, new java.util.Date(exit.getTimestamp()) + " pid=" + exit.getPid()
                            + " reason=" + exit.getReason() + " status=" + exit.getStatus()
                            + " " + exit.getDescription() + "\n");
                }
            }
        }
        writeText(output, "\n=== App-visible Android logcat ===\n");
        Process process = null;
        try {
            process = new ProcessBuilder("/system/bin/logcat", "-d", "-t", "400",
                    "-b", "main", "-b", "system", "-b", "crash", "-v", "threadtime",
                    "--uid=" + android.os.Process.myUid())
                    .redirectErrorStream(true).start();
            try (InputStream input = process.getInputStream()) {
                byte[] buffer = new byte[8192];
                int remaining = 512 * 1024;
                while (remaining > 0) {
                    int count = input.read(buffer, 0, Math.min(buffer.length, remaining));
                    if (count < 0) break;
                    output.write(buffer, 0, count);
                    remaining -= count;
                }
            }
        } catch (java.io.IOException error) {
            writeText(output, "Android logcat unavailable: " + error + "\n");
        } finally {
            if (process != null) process.destroy();
        }
    }

    private static void writeText(OutputStream output, String text) throws java.io.IOException {
        output.write(text.getBytes(java.nio.charset.StandardCharsets.UTF_8));
    }

    static void exportAsync(Context context) {
        Context app = context.getApplicationContext();
        new Thread(() -> export(app), "MCLA log export").start();
    }

    private static void export(Context context) {
        if (Build.VERSION.SDK_INT < 29) {
            Log.w(TAG, "Automatic Documents export requires Android 10 or newer");
            return;
        }
        synchronized (LOCK) {
            File source = new File(context.getFilesDir(), "logs");
            if (android.os.Build.VERSION.SDK_INT >= 30) exportCrashReason(context);
            File[] logs = source.listFiles((dir, name) -> name.startsWith("mcla_") &&
                    name.endsWith(".log"));
            if (logs == null) return;
            Arrays.sort(logs, (left, right) -> left.getName().compareTo(right.getName()));
            SharedPreferences state = context.getSharedPreferences("exported-logs", Context.MODE_PRIVATE);
            File current = new File(source, "mcla.log");
            if (current.isFile()) {
                // Export the bounded rotation and current log as ONE document,
                // oldest first. Preserve historical exports from older builds.
                File backup = new File(source, "mcla.1.log");
                File[] parts = backup.isFile() ? new File[]{backup, current} :
                        new File[]{current};
                String stamp = "";
                for (File part : parts) stamp += part.length() + ":" + part.lastModified() + ";";
                if (!stamp.equals(state.getString("mcla-current.log.stamp", ""))) {
                    try {
                        exportOne(context.getContentResolver(), state, "mcla-current.log", parts);
                        state.edit().putString("mcla-current.log.stamp", stamp).apply();
                    } catch (Exception e) {
                        Log.e(TAG, "Could not export current log", e);
                    }
                }
                return;
            }
            for (File log : logs) {
                if (!log.isFile()) continue;
                String name = log.getName();
                String stamp = log.length() + ":" + log.lastModified();
                if (stamp.equals(state.getString(name + ".stamp", "")) &&
                        "txt".equals(state.getString(name + ".format", ""))) continue;
                try {
                    exportOne(context.getContentResolver(), state, name, new File[]{log});
                    state.edit().putString(name + ".stamp", stamp)
                            .putString(name + ".format", "txt").apply();
                } catch (Exception e) {
                    Log.e(TAG, "Could not export " + name + " to " + RELATIVE_PATH, e);
                }
            }
        }
    }

    private static void exportCrashReason(Context context) {
        if (android.os.Build.VERSION.SDK_INT < 30) return;
        try {
            ActivityManager manager = context.getSystemService(ActivityManager.class);
            if (manager == null) return;
            for (ApplicationExitInfo exit : manager.getHistoricalProcessExitReasons(null, 0, 8)) {
                int reason = exit.getReason();
                if (reason != ApplicationExitInfo.REASON_CRASH_NATIVE &&
                        reason != ApplicationExitInfo.REASON_CRASH &&
                        reason != ApplicationExitInfo.REASON_SIGNALED &&
                        reason != ApplicationExitInfo.REASON_LOW_MEMORY) continue;
                SharedPreferences state =
                        context.getSharedPreferences("exported-logs", Context.MODE_PRIVATE);
                if (state.getLong("last-exit", 0) >= exit.getTimestamp()) return;
                File report = File.createTempFile("mcla-exit-", ".tmp", context.getCacheDir());
                try {
                    try (java.io.FileOutputStream output = new java.io.FileOutputStream(report)) {
                        String summary = "Android exit reason: " + reason + ", status: "
                                + exit.getStatus() + ", pid: " + exit.getPid() + ", time: "
                                + new java.util.Date(exit.getTimestamp()) + "\n"
                                + exit.getDescription() + "\n";
                        output.write(summary.getBytes(java.nio.charset.StandardCharsets.UTF_8));
                    }
                    exportOne(context.getContentResolver(), state, "mcla-android-exit.log",
                            new File[]{report});
                    state.edit().putLong("last-exit", exit.getTimestamp()).apply();
                } finally {
                    report.delete();
                }
                return;
            }
        } catch (Exception error) {
            Log.w(TAG, "Could not export the Android process exit reason", error);
        }
    }

    private static void exportOne(ContentResolver resolver, SharedPreferences state,
            String name, File[] parts) throws Exception {
        String exportName = name.substring(0, name.length() - 4) + ".txt";
        String saved = state.getString(name + ".uri", null);
        Uri target = saved == null ? null : Uri.parse(saved);
        boolean fresh = target == null;
        if (fresh) {
            ContentValues values = new ContentValues();
            values.put(MediaStore.MediaColumns.DISPLAY_NAME, exportName);
            values.put(MediaStore.MediaColumns.MIME_TYPE, "text/plain");
            values.put(MediaStore.MediaColumns.RELATIVE_PATH, RELATIVE_PATH);
            values.put(MediaStore.MediaColumns.IS_PENDING, 1);
            target = resolver.insert(MediaStore.Files.getContentUri(
                    MediaStore.VOLUME_EXTERNAL_PRIMARY), values);
            if (target == null) throw new Exception("MediaStore refused " + RELATIVE_PATH + name);
        } else {
            ContentValues rename = new ContentValues();
            rename.put(MediaStore.MediaColumns.DISPLAY_NAME, exportName);
            if (resolver.update(target, rename, null, null) != 1) {
                state.edit().remove(name + ".uri").apply();
                throw new Exception("Could not rename exported log " + name);
            }
        }
        try (OutputStream output = resolver.openOutputStream(target, "wt")) {
            if (output == null) throw new Exception("Cannot open Documents output");
            byte[] buffer = new byte[64 * 1024];
            for (File part : parts) {
                try (InputStream input = new FileInputStream(part)) {
                    int count;
                    while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
                }
            }
            output.flush();
        } catch (Exception e) {
            if (fresh) resolver.delete(target, null, null);
            else state.edit().remove(name + ".uri").apply();
            throw e;
        }
        if (fresh) {
            ContentValues published = new ContentValues();
            published.put(MediaStore.MediaColumns.IS_PENDING, 0);
            if (resolver.update(target, published, null, null) != 1)
                throw new Exception("Could not publish exported log " + name);
            state.edit().putString(name + ".uri", target.toString()).apply();
        }
        Log.i(TAG, "Exported " + name + " to " + RELATIVE_PATH);
    }
}
