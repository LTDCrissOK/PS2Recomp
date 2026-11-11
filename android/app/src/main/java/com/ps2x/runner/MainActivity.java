package com.ps2x.runner;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.graphics.Color;
import android.graphics.Typeface;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

public class MainActivity extends Activity {
    private static final int REQUEST_ELF = 4101;
    private TextView status;
    private File selectedElf;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        buildMenu();
        File expected = new File(getExternalFilesDir(null), "game.elf");
        if (expected.isFile() && expected.length() > 0) {
            selectedElf = expected;
            status.setText("ELF preparado: " + expected.getName() + "\nTamaño: " + expected.length() + " bytes");
        }
    }

    private void buildMenu() {
        getWindow().setStatusBarColor(Color.rgb(20, 0, 0));
        getWindow().setNavigationBarColor(Color.BLACK);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        root.setPadding(dp(24), dp(36), dp(24), dp(24));
        root.setBackgroundColor(Color.rgb(12, 12, 14));

        TextView title = new TextView(this);
        title.setText("PS2 RECOMP");
        title.setTextColor(Color.rgb(220, 35, 35));
        title.setTextSize(30);
        title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        title.setGravity(Gravity.CENTER);
        root.addView(title, new LinearLayout.LayoutParams(-1, dp(58)));

        TextView subtitle = new TextView(this);
        subtitle.setText("LANZADOR ANDROID");
        subtitle.setTextColor(Color.LTGRAY);
        subtitle.setTextSize(13);
        subtitle.setGravity(Gravity.CENTER);
        root.addView(subtitle, new LinearLayout.LayoutParams(-1, dp(34)));

        status = new TextView(this);
        status.setText("No hay ningún ELF seleccionado.\nSeleccioná un ELF recompilado para continuar.");
        status.setTextColor(Color.WHITE);
        status.setTextSize(15);
        status.setGravity(Gravity.CENTER);
        status.setPadding(dp(12), dp(18), dp(12), dp(18));
        root.addView(status, new LinearLayout.LayoutParams(-1, dp(110)));

        Button choose = makeButton("SELECCIONAR ELF RECOMPILADO");
        choose.setOnClickListener(v -> openElfPicker());
        root.addView(choose, buttonParams());

        Button start = makeButton("INICIAR JUEGO");
        start.setOnClickListener(v -> launchRuntime());
        root.addView(start, buttonParams());

        Button iso = makeButton("¿PUEDO ABRIR UNA ISO?");
        iso.setOnClickListener(v -> new AlertDialog.Builder(this)
            .setTitle("Compatibilidad de ISO")
            .setMessage("Esta aplicación está basada en PS2Recomp: no es un emulador y no ejecuta ISOs directamente. Actualmente necesita un ELF de PS2 previamente recompilado. La carga y conversión de ISO a un proyecto recompilable todavía no está implementada.")
            .setPositiveButton("ENTENDIDO", null)
            .show());
        root.addView(iso, buttonParams());

        TextView footer = new TextView(this);
        footer.setText("ARM32 / ARM64  •  Runtime nativo");
        footer.setTextColor(Color.GRAY);
        footer.setTextSize(12);
        footer.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams footerParams = new LinearLayout.LayoutParams(-1, dp(54));
        footerParams.topMargin = dp(20);
        root.addView(footer, footerParams);

        setContentView(root);
    }

    private Button makeButton(String label) {
        Button b = new Button(this);
        b.setText(label);
        b.setTextColor(Color.WHITE);
        b.setTextSize(14);
        b.setAllCaps(false);
        b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(Color.rgb(145, 18, 25)));
        return b;
    }

    private LinearLayout.LayoutParams buttonParams() {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1, dp(56));
        p.topMargin = dp(12);
        return p;
    }

    private int dp(int value) {
        return (int)(value * getResources().getDisplayMetrics().density + 0.5f);
    }

    private void openElfPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQUEST_ELF);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_ELF || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        try (InputStream in = getContentResolver().openInputStream(uri)) {
            if (in == null) throw new Exception("No se pudo abrir el archivo.");
            File target = new File(getExternalFilesDir(null), "game.elf");
            byte[] magic = new byte[4];
            int count = in.read(magic);
            if (count != 4 || magic[0] != 0x7f || magic[1] != 'E' || magic[2] != 'L' || magic[3] != 'F') {
                throw new Exception("El archivo seleccionado no tiene una cabecera ELF válida.");
            }
            try (FileOutputStream out = new FileOutputStream(target)) {
                out.write(magic);
                byte[] buffer = new byte[64 * 1024];
                int n;
                while ((n = in.read(buffer)) != -1) out.write(buffer, 0, n);
                out.flush();
            }
            selectedElf = target;
            status.setText("ELF preparado correctamente:\n" + target.getName() + "\n" + target.length() + " bytes");
        } catch (Exception e) {
            selectedElf = null;
            status.setText("No se pudo preparar el ELF: " + e.getMessage());
            new AlertDialog.Builder(this).setTitle("Error al cargar ELF")
                .setMessage(e.getMessage()).setPositiveButton("ACEPTAR", null).show();
        }
    }

    private void launchRuntime() {
        if (selectedElf == null || !selectedElf.isFile() || selectedElf.length() < 4) {
            new AlertDialog.Builder(this).setTitle("Falta seleccionar un ELF")
                .setMessage("Primero seleccioná un ELF recompilado válido.")
                .setPositiveButton("ACEPTAR", null).show();
            return;
        }
        Intent intent = new Intent(this, android.app.NativeActivity.class);
        startActivity(intent);
    }
}
