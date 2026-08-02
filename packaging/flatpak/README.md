# Flatpak build

This package is compiled inside `org.kde.Sdk//6.10` and runs on
`org.kde.Platform//6.10`. Qt, the KDE platform theme, Wayland/X11 platform
plugins, and Breeze therefore all come from the same KDE runtime under `/usr`.
The app does not bundle `libQt6*.so` or set a custom `QT_PLUGIN_PATH`.

Install the build dependencies and build from the repository root:

On Debian/Ubuntu, `flatpak-builder` needs the separate `appstream-compose`
package when exporting a repository:

```sh
sudo apt-get install flatpak flatpak-builder appstream appstream-compose
```

```sh
flatpak remote-add --user --if-not-exists flathub \
  https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub org.kde.Platform//6.10 org.kde.Sdk//6.10
flatpak-builder --user --install-deps-from=flathub --force-clean \
  build/flatpak packaging/flatpak/io.github.mybna134.ATLabelMaster.yml
```

Run the uninstalled build:

```sh
flatpak-builder --run build/flatpak \
  packaging/flatpak/io.github.mybna134.ATLabelMaster.yml LabelMaster
```

The manifest sets `QT_QPA_PLATFORMTHEME=kde`. The corresponding
`KDEPlasmaPlatformTheme6.so` is intentionally loaded from the KDE runtime,
instead of being copied to `/app`.
