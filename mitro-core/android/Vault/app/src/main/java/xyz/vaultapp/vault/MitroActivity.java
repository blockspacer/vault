package xyz.vaultapp.vault;

import android.annotation.SuppressLint;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Bundle;
import android.support.v7.app.ActionBarActivity;
import android.support.v7.app.AppCompatActivity;
import android.util.Log;

import com.google.android.gms.analytics.HitBuilders;
import com.google.android.gms.analytics.Tracker;


public class MitroActivity extends AppCompatActivity {
  private BroadcastReceiver receiver = null;

  public MitroApplication getApp() {
    return (MitroApplication) getApplicationContext();
  }

  @SuppressLint("NewApi")
  @SuppressWarnings("deprecation")
  public void copyText(String label, String text) {
    if (android.os.Build.VERSION.SDK_INT < 11) {
        android.text.ClipboardManager clipboard = (android.text.ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
        clipboard.setText(text);
    } else {
      android.content.ClipboardManager clipboard = (android.content.ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
      android.content.ClipData clip = android.content.ClipData.newPlainText(label, text);
      clipboard.setPrimaryClip(clip);
    }
  }

  public void maybeLogout() {
    // Log out unless "Keep me logged in" has been selected, in which case, privateKey will be set.
    if (getApp().isLoggedIn() &&
        getApp().getSharedPreference("privateKey") == null) {
      Log.d("Mitro", "logging out");
      getApp().getSecretManager().clear();
      getApp().getApiClient().logout();
    }
  }

  public class ScreenReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
      if (intent.getAction().equals(Intent.ACTION_SCREEN_OFF)) {
        // Log out on screen off unless user has checked "Keep me logged in"
        maybeLogout();
      }
    }
  }

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);

    IntentFilter filter = new IntentFilter(Intent.ACTION_SCREEN_ON);
    filter.addAction(Intent.ACTION_SCREEN_OFF);
    receiver = new ScreenReceiver();
    registerReceiver(receiver, filter);
  }

  @Override
  protected void onDestroy() {
    if (receiver != null) {
      unregisterReceiver(receiver);
    }
    if (getApp().isLoggedIn() && this instanceof SecretListActivity) {
      // Main activity is being destroyed.  We will not receive screen off events in this state
      // so we should log out.
      maybeLogout();
    }
    super.onDestroy();
  }

  @Override
  public void onStart() {
    super.onStart();

    if (!getApp().isLoggedIn() && !(this instanceof LoginActivity)) {
      Log.d("Mitro", "starting login activity");
      Intent loginActivity = new Intent(MitroActivity.this, LoginActivity.class);
      startActivity(loginActivity);
      finish();
    }

    // Track activity hit
    Tracker mTracker = getApp().getDefaultTracker();
    mTracker.setScreenName(this.getClass().getSimpleName());
    mTracker.send(new HitBuilders.ScreenViewBuilder().build());
  }

  @Override
  public void onStop() {
    super.onStop();
  }
}
