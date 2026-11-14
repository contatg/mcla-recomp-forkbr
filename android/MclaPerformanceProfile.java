package org.mcla.recomp;

import android.content.Context;
import android.os.Build;

final class MclaPerformanceProfile {
    static final String[] LABELS = {"Automatic", "Standard", "Performance", "Maximum performance"};
    private static final String[] VALUES = {"auto", "standard", "performance", "maximum"};

    static int selected(Context context) {
        String saved = context.getSharedPreferences("graphics", Context.MODE_PRIVATE)
                .getString("performance_profile", VALUES[0]);
        for (int index = 0; index < VALUES.length; index++) {
            if (VALUES[index].equals(saved)) return index;
        }
        return 0;
    }

    static void select(Context context, int index) {
        context.getSharedPreferences("graphics", Context.MODE_PRIVATE).edit()
                .putString("performance_profile", VALUES[index]).apply();
    }

    static String resolve(Context context) {
        if (MclaBuildVariant.isMali(context) || MclaBuildVariant.isRedMagicAudioTest(context)) {
            return "standard";
        }
        int choice = selected(context);
        if (choice != 0) return VALUES[choice];
        return Build.VERSION.SDK_INT >= 31 && "SM6225".equalsIgnoreCase(Build.SOC_MODEL)
                ? "performance" : "standard";
    }

    static String defaultTurnipDebug(Context context) {
        return "standard".equals(resolve(context)) ? "syncdraw" : "";
    }

    private MclaPerformanceProfile() {}
}
