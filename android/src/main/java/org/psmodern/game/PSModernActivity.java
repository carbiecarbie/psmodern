// SPDX-License-Identifier: GPL-3.0-or-later
package org.psmodern.game;

import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.DisplayCutout;
import android.view.View;
import android.view.WindowInsets;

import org.libsdl.app.SDLActivity;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Locale;
import java.util.concurrent.CountDownLatch;

public final class PSModernActivity extends SDLActivity {
    private static final int PICK_ROM = 901;
    private static final int LIMIT = 512 * 1024 + 1;
    private static final class Pick {
        final CountDownLatch ready = new CountDownLatch(1);
        byte[] bytes;
        String suffix, error;
    }
    private Pick pending;
    private boolean closing;

    // SDLThread can be inside chooseDocument when SDLActivity joins it in onDestroy.
    // A provider may block indefinitely. Only SDLThread writes the staging file.
    public String chooseDocument() throws IOException, InterruptedException {
        final Pick pick = new Pick();
        synchronized (this) {
            if (closing) return null;
            pending = pick;
        }
        runOnUiThread(() -> {
            if (closing || isFinishing()) { finishPick(pick, null, null, null); return; }
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT)
                .addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
            try { startActivityForResult(intent, PICK_ROM); }
            catch (RuntimeException e) { finishPick(pick, null, null, e.toString()); }
        });
        pick.ready.await();
        if (pick.error != null) throw new IOException(pick.error);
        if (pick.suffix == null) return null;
        File folder = getExternalFilesDir(null);
        if (folder == null) throw new IOException("App storage is unavailable.");
        File stage = new File(new File(folder, "roms"), ".import" + pick.suffix);
        try (FileOutputStream out = new FileOutputStream(stage)) {
            out.write(pick.bytes);
            out.getFD().sync();
        }
        return pick.suffix;
    }

    private synchronized void finishPick(Pick pick, String suffix, byte[] bytes, String error) {
        if (pending != pick) return;
        pick.suffix = suffix;
        pick.bytes = bytes;
        pick.error = error;
        pending = null;
        pick.ready.countDown();
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_ROM) return;
        final Pick pick;
        synchronized (this) { pick = pending; }
        if (pick == null) return;
        Uri uri = data == null ? null : data.getData();
        if (result != RESULT_OK || uri == null) { finishPick(pick, null, null, null); return; }
        new Thread(() -> {
            try {
                String name = "";
                try (Cursor cursor = getContentResolver().query(uri, new String[]{OpenableColumns.DISPLAY_NAME},
                                                                null, null, null)) {
                    if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0))
                        name = cursor.getString(0).toLowerCase(Locale.ROOT);
                }
                if (name.endsWith(".zip")) { finishPick(pick, ".zip", new byte[0], null); return; }
                if (!name.endsWith(".sms")) throw new IOException("Expected an uncompressed .sms document.");
                try (InputStream in = getContentResolver().openInputStream(uri)) {
                    if (in == null) throw new IOException("Cannot open selected document.");
                    ByteArrayOutputStream out = new ByteArrayOutputStream();
                    byte[] buffer = new byte[8192];
                    while (out.size() < LIMIT) {
                        int n = in.read(buffer, 0, Math.min(buffer.length, LIMIT - out.size()));
                        if (n < 0) break;
                        if (n == 0) throw new IOException("Document provider stopped reading.");
                        out.write(buffer, 0, n);
                    }
                    finishPick(pick, ".sms", out.toByteArray(), null);
                }
            } catch (Exception e) {
                Log.w("PSModern", "Could not import document", e);
                finishPick(pick, null, null, e.toString());
            }
        }, "ROM reader").start();
    }

    @Override protected void onDestroy() {
        synchronized (this) {
            closing = true;
            if (pending != null) finishPick(pending, null, null, null);
        }
        super.onDestroy();
    }

    private static native void setInsets(int top, int left, int bottom, int right);
    private void updateInsets(View surface) {
        WindowInsets i = surface.getRootWindowInsets();
        if (i == null || surface.getWidth() == 0) return;
        // SDL's fullscreen style hides the status and navigation bars but sets
        // SYSTEM_UI_FLAG_LAYOUT_STABLE, under which the system-window insets
        // keep reporting the hidden bars. While it is active only the display
        // cutout is kept clear; a bar swiped into view overlays the game.
        int t = 0, l = 0, b = 0, r = 0;
        if (!mFullscreenModeActive) {
            t = i.getSystemWindowInsetTop(); l = i.getSystemWindowInsetLeft();
            b = i.getSystemWindowInsetBottom(); r = i.getSystemWindowInsetRight();
        }
        if (Build.VERSION.SDK_INT >= 28) {
            DisplayCutout c = i.getDisplayCutout();
            if (c != null) {
                t = Math.max(t, c.getSafeInsetTop()); l = Math.max(l, c.getSafeInsetLeft());
                b = Math.max(b, c.getSafeInsetBottom()); r = Math.max(r, c.getSafeInsetRight());
            }
        }
        View root = surface.getRootView();
        int[] pos = new int[2];
        surface.getLocationInWindow(pos);
        setInsets(Math.max(0,t-pos[1]), Math.max(0,l-pos[0]),
                  Math.max(0,b-root.getHeight()+pos[1]+surface.getHeight()),
                  Math.max(0,r-root.getWidth()+pos[0]+surface.getWidth()));
    }
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        View surface = mSurface;
        if (surface != null) {
            surface.addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or_,ob) -> updateInsets(v));
            surface.setOnApplyWindowInsetsListener((v,i) -> { v.post(() -> updateInsets(v)); return i; });
        }
    }
}
