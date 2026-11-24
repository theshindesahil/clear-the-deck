# Windows 11 style confirmation dialog (WPF) for Game Mode. Dot-sourced by gamemode.ps1.
# Follows the Windows app theme (dark/light), crisp at any DPI, with an "Advanced options"
# checklist (Select all) to choose what to close/reopen this run.

function Get-IsDarkTheme {
    try { return ((Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize' -ErrorAction Stop).AppsUseLightTheme -eq 0) } catch { return $true }
}

function Initialize-GmUi {
    if ('GmWin' -as [type]) { return }
    Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase, System.Xaml, System.Drawing
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class GmWin {
    [DllImport("dwmapi.dll")] public static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int val, int size);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
}
'@
}

# Accent-colored rounded square with a game controller glyph, used as the window/taskbar icon.
function New-GmIcon {
    $size = 64
    $dv = New-Object System.Windows.Media.DrawingVisual
    $dc = $dv.RenderOpen()
    $accent = [System.Windows.Media.ColorConverter]::ConvertFromString('#F2512D')
    $dc.DrawRoundedRectangle((New-Object System.Windows.Media.SolidColorBrush($accent)), $null, (New-Object System.Windows.Rect(0, 0, $size, $size)), 14, 14)
    $tf = New-Object System.Windows.Media.Typeface((New-Object System.Windows.Media.FontFamily('Segoe Fluent Icons, Segoe MDL2 Assets')), [System.Windows.FontStyles]::Normal, [System.Windows.FontWeights]::Normal, [System.Windows.FontStretches]::Normal)
    $ft = New-Object System.Windows.Media.FormattedText([string][char]0xE7FC, [System.Globalization.CultureInfo]::InvariantCulture, [System.Windows.FlowDirection]::LeftToRight, $tf, 38, [System.Windows.Media.Brushes]::White, 1.0)
    $dc.DrawText($ft, (New-Object System.Windows.Point((($size - $ft.Width) / 2), (($size - $ft.Height) / 2))))
    $dc.Close()
    $rtb = New-Object System.Windows.Media.Imaging.RenderTargetBitmap($size, $size, 96, 96, [System.Windows.Media.PixelFormats]::Pbgra32)
    $rtb.Render($dv); $rtb.Freeze()
    return $rtb
}

$script:GmXaml = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="Game Mode" Width="470" SizeToContent="Height" ResizeMode="NoResize"
        WindowStartupLocation="CenterScreen" Topmost="True" ShowInTaskbar="True"
        Background="{DynamicResource BgBrush}" Foreground="{DynamicResource TextBrush}"
        FontFamily="Segoe UI Variable Text, Segoe UI" FontSize="14"
        UseLayoutRounding="True" SnapsToDevicePixels="True"
        TextOptions.TextFormattingMode="Ideal" TextOptions.TextRenderingMode="ClearType">
  <Window.Resources>
    <SolidColorBrush x:Key="BgBrush" Color="@@bg@@"/>
    <SolidColorBrush x:Key="FooterBrush" Color="@@footer@@"/>
    <SolidColorBrush x:Key="CardBrush" Color="@@card@@"/>
    <SolidColorBrush x:Key="SoftStroke" Color="@@soft@@"/>
    <SolidColorBrush x:Key="BoxStroke" Color="@@boxstroke@@"/>
    <SolidColorBrush x:Key="TextBrush" Color="@@text@@"/>
    <SolidColorBrush x:Key="MutedBrush" Color="@@muted@@"/>
    <SolidColorBrush x:Key="BtnBrush" Color="@@btn@@"/>
    <SolidColorBrush x:Key="BtnStroke" Color="@@btnstroke@@"/>
    <SolidColorBrush x:Key="AccentBrush" Color="@@accent@@"/>
    <SolidColorBrush x:Key="AccentTextBrush" Color="@@accenttext@@"/>

    <Style x:Key="Fluent.CheckBox" TargetType="CheckBox">
      <Setter Property="Foreground" Value="{DynamicResource TextBrush}"/>
      <Setter Property="Cursor" Value="Hand"/>
      <Setter Property="Margin" Value="0,3"/>
      <Setter Property="FocusVisualStyle" Value="{x:Null}"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="CheckBox">
            <Grid Background="Transparent">
              <Grid.ColumnDefinitions>
                <ColumnDefinition Width="Auto"/>
                <ColumnDefinition Width="*"/>
              </Grid.ColumnDefinitions>
              <Border x:Name="Box" Width="20" Height="20" CornerRadius="4" BorderThickness="1.5"
                      BorderBrush="{DynamicResource BoxStroke}" Background="Transparent" VerticalAlignment="Center">
                <Path x:Name="Tick" Data="M3,8.5 L7,12.5 L14.5,4" Stroke="White" StrokeThickness="2"
                      StrokeStartLineCap="Round" StrokeEndLineCap="Round" StrokeLineJoin="Round"
                      Visibility="Collapsed" HorizontalAlignment="Center" VerticalAlignment="Center"/>
              </Border>
              <ContentPresenter Grid.Column="1" Margin="10,0,0,0" VerticalAlignment="Center"/>
            </Grid>
            <ControlTemplate.Triggers>
              <Trigger Property="IsMouseOver" Value="True">
                <Setter TargetName="Box" Property="BorderBrush" Value="{DynamicResource TextBrush}"/>
              </Trigger>
              <Trigger Property="IsChecked" Value="True">
                <Setter TargetName="Box" Property="Background" Value="{DynamicResource AccentBrush}"/>
                <Setter TargetName="Box" Property="BorderBrush" Value="{DynamicResource BoxStroke}"/>
                <Setter TargetName="Tick" Property="Visibility" Value="Visible"/>
              </Trigger>
              <Trigger Property="IsKeyboardFocused" Value="True">
                <Setter TargetName="Box" Property="BorderBrush" Value="{DynamicResource TextBrush}"/>
                <Setter TargetName="Box" Property="BorderThickness" Value="3"/>
              </Trigger>
            </ControlTemplate.Triggers>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>

    <Style x:Key="Fluent.Button" TargetType="Button">
      <Setter Property="Height" Value="36"/>
      <Setter Property="Cursor" Value="Hand"/>
      <Setter Property="FontWeight" Value="SemiBold"/>
      <Setter Property="Background" Value="{DynamicResource BtnBrush}"/>
      <Setter Property="BorderBrush" Value="{DynamicResource BtnStroke}"/>
      <Setter Property="Foreground" Value="{DynamicResource TextBrush}"/>
      <Setter Property="FocusVisualStyle" Value="{x:Null}"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="Button">
            <Border x:Name="Bd" Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}"
                    BorderThickness="1" CornerRadius="4">
              <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
            </Border>
            <ControlTemplate.Triggers>
              <Trigger Property="IsMouseOver" Value="True"><Setter TargetName="Bd" Property="Opacity" Value="0.88"/></Trigger>
              <Trigger Property="IsPressed" Value="True"><Setter TargetName="Bd" Property="Opacity" Value="0.7"/></Trigger>
              <Trigger Property="IsDefault" Value="True"><Setter TargetName="Bd" Property="BorderBrush" Value="{DynamicResource AccentBrush}"/></Trigger>
              <Trigger Property="IsKeyboardFocused" Value="True">
                <Setter TargetName="Bd" Property="BorderBrush" Value="{DynamicResource TextBrush}"/>
                <Setter TargetName="Bd" Property="BorderThickness" Value="2.5"/>
              </Trigger>
            </ControlTemplate.Triggers>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>
  </Window.Resources>

  <Grid>
    <Grid.RowDefinitions>
      <RowDefinition Height="Auto"/>
      <RowDefinition Height="Auto"/>
    </Grid.RowDefinitions>

    <StackPanel Grid.Row="0" Margin="24,24,24,22">
      <TextBlock x:Name="TitleText" FontSize="20" FontWeight="SemiBold" TextWrapping="Wrap"/>
      <StackPanel x:Name="Summary" Margin="0,14,0,0"/>

      <Border x:Name="AdvHeader" Margin="0,16,0,0" Cursor="Hand" Background="Transparent" HorizontalAlignment="Left"
              Focusable="True" AutomationProperties.Name="Advanced options"
              AutomationProperties.HelpText="Press Space or Enter to show or hide the list of apps. Untick an app to leave it alone this run.">
        <StackPanel Orientation="Horizontal">
          <TextBlock Text="Advanced options" Foreground="{DynamicResource AccentTextBrush}" FontSize="13" FontWeight="SemiBold" VerticalAlignment="Center"/>
          <Path x:Name="Chevron" Data="M0,0 L5,5 L10,0" Stroke="{DynamicResource AccentTextBrush}" StrokeThickness="1.6"
                Margin="9,2,0,0" VerticalAlignment="Center" RenderTransformOrigin="0.5,0.5"
                StrokeStartLineCap="Round" StrokeEndLineCap="Round" StrokeLineJoin="Round"/>
        </StackPanel>
      </Border>

      <Border x:Name="AdvBody" Visibility="Collapsed" Margin="0,10,0,0" Padding="16,10,16,10" CornerRadius="8"
              Background="{DynamicResource CardBrush}" BorderBrush="{DynamicResource SoftStroke}" BorderThickness="1">
        <StackPanel>
          <CheckBox x:Name="SelAll" Content="Select all" Style="{StaticResource Fluent.CheckBox}" FontWeight="SemiBold"/>
          <Rectangle Height="1" Fill="{DynamicResource SoftStroke}" Margin="0,6,0,6"/>
          <UniformGrid x:Name="AppGrid" Columns="2"/>
          <StackPanel x:Name="SvcPanel"/>
        </StackPanel>
      </Border>

      <TextBlock x:Name="HintText" Margin="0,16,0,0" FontSize="12" Foreground="{DynamicResource MutedBrush}" TextWrapping="Wrap"/>
    </StackPanel>

    <Border Grid.Row="1" Background="{DynamicResource FooterBrush}" BorderBrush="{DynamicResource SoftStroke}"
            BorderThickness="0,1,0,0" Padding="24,16,24,16">
      <Grid>
        <Grid.ColumnDefinitions>
          <ColumnDefinition Width="*"/>
          <ColumnDefinition Width="12"/>
          <ColumnDefinition Width="*"/>
        </Grid.ColumnDefinitions>
        <Button x:Name="YesBtn" Grid.Column="0" Style="{StaticResource Fluent.Button}" IsDefault="True"
                Background="{DynamicResource AccentBrush}" BorderBrush="{DynamicResource AccentBrush}" Foreground="White"/>
        <Button x:Name="NoBtn" Grid.Column="2" Style="{StaticResource Fluent.Button}" IsCancel="True" Content="Cancel"/>
      </Grid>
    </Border>
  </Grid>
</Window>
'@

# $items: objects with Key, Label, Kind ('app' or 'service'). Returns @{ Confirmed; Keys } (Keys = ticked items).
function Show-GameModeDialog($headline, $items, $verbApp, $verbSvc, $infoLines, $hint, $yesText, $shotPath, $startExpanded) {
    Initialize-GmUi
    $dark = Get-IsDarkTheme
    if ($dark) { $p = @{ bg = '#2B2B2B'; footer = '#202020'; card = '#333333'; soft = '#454545'; boxstroke = '#A6A6A6'; text = '#FFFFFF'; muted = '#B5B5B5'; btn = '#3C3C3C'; btnstroke = '#4D4D4D'; accent = '#CF3C1A'; accenttext = '#FF7F5E' } }
    else       { $p = @{ bg = '#F9F9F9'; footer = '#F0F0F0'; card = '#FFFFFF'; soft = '#E3E3E3'; boxstroke = '#6B6B6B'; text = '#1A1A1A'; muted = '#5F5F5F'; btn = '#FDFDFD'; btnstroke = '#D9D9D9'; accent = '#CF3C1A'; accenttext = '#C93A18' } }
    $xaml = $script:GmXaml
    foreach ($k in $p.Keys) { $xaml = $xaml.Replace("@@$k@@", $p[$k]) }
    $win = [Windows.Markup.XamlReader]::Parse($xaml)
    try { $win.Icon = New-GmIcon } catch { }

    $title = $win.FindName('TitleText'); $summary = $win.FindName('Summary'); $advHeader = $win.FindName('AdvHeader')
    $advBody = $win.FindName('AdvBody'); $chevron = $win.FindName('Chevron'); $selAll = $win.FindName('SelAll')
    $appGrid = $win.FindName('AppGrid'); $svcPanel = $win.FindName('SvcPanel'); $hintText = $win.FindName('HintText')
    $yes = $win.FindName('YesBtn'); $no = $win.FindName('NoBtn')

    $title.Text = $headline; $hintText.Text = $hint; $yes.Content = $yesText
    $apps = @($items | Where-Object { $_.Kind -eq 'app' })
    $svcs = @($items | Where-Object { $_.Kind -eq 'service' })
    $st = @{ busy = $false }

    $brushText = $win.FindResource('TextBrush'); $brushMuted = $win.FindResource('MutedBrush')
    $line1 = New-Object System.Windows.Controls.TextBlock; $line1.Foreground = $brushText; $line1.Margin = '0,0,0,4'
    $line2 = New-Object System.Windows.Controls.TextBlock; $line2.Foreground = $brushText; $line2.Margin = '0,0,0,4'
    [void]$summary.Children.Add($line1); [void]$summary.Children.Add($line2)
    foreach ($l in @($infoLines)) {
        $t = New-Object System.Windows.Controls.TextBlock; $t.Text = $l; $t.Foreground = $brushMuted; $t.FontSize = 13; $t.Margin = '0,0,0,4'; $t.TextWrapping = 'Wrap'
        [void]$summary.Children.Add($t)
    }

    $cbStyle = $win.FindResource('Fluent.CheckBox')
    $checks = @()
    foreach ($it in $items) {
        $cb = New-Object System.Windows.Controls.CheckBox
        $tb = New-Object System.Windows.Controls.TextBlock; $tb.Text = $it.Label; $tb.TextTrimming = 'CharacterEllipsis'   # long names end in ...
        $cb.Style = $cbStyle; $cb.Content = $tb; $cb.ToolTip = $it.Label; $cb.IsChecked = $true; $cb.Tag = $it
        [System.Windows.Automation.AutomationProperties]::SetName($cb, [string]$it.Label)
        if ($it.Kind -eq 'app') { [void]$appGrid.Children.Add($cb) } else { [void]$svcPanel.Children.Add($cb) }
        $checks += $cb
    }

    $updateSummary = {
        $aSel = @($checks | Where-Object { $_.IsChecked -and $_.Tag.Kind -eq 'app' }).Count
        $sSel = @($checks | Where-Object { $_.IsChecked -and $_.Tag.Kind -eq 'service' }).Count
        $line1.Text = if ($apps.Count -eq 0) { "$verbApp nothing" } else { "$verbApp $aSel of $($apps.Count) apps" }
        $line2.Text = if ($svcs.Count -eq 0) { "$verbSvc nothing" } else { "$verbSvc $sSel of $($svcs.Count) service" + $(if ($svcs.Count -ne 1) { 's' } else { '' }) }
    }
    $setExpanded = {
        param($on)
        $advBody.Visibility = $(if ($on) { 'Visible' } else { 'Collapsed' })
        $chevron.RenderTransform = New-Object System.Windows.Media.RotateTransform($(if ($on) { 180 } else { 0 }))
    }

    if ($items.Count -eq 0) { $advHeader.Visibility = 'Collapsed' }
    & $setExpanded ([bool]$startExpanded)
    $advHeader.Add_MouseLeftButtonUp({ & $setExpanded ($advBody.Visibility -ne 'Visible') })
    $advHeader.Add_KeyDown({
        param($s, $e)
        if ($e.Key -eq 'Space' -or $e.Key -eq 'Return') { & $setExpanded ($advBody.Visibility -ne 'Visible'); $e.Handled = $true }
    })
    $selAll.IsChecked = $true
    $selAll.Add_Click({
        if ($st.busy) { return }
        $st.busy = $true; foreach ($c in $checks) { $c.IsChecked = $selAll.IsChecked }; $st.busy = $false
        & $updateSummary
    })
    foreach ($c in $checks) {
        $c.Add_Click({
            $st.busy = $true; $selAll.IsChecked = (@($checks | Where-Object { -not $_.IsChecked }).Count -eq 0); $st.busy = $false
            & $updateSummary
        })
    }
    $yes.Add_Click({ $win.DialogResult = $true })
    $no.Add_Click({ $win.DialogResult = $false })

    $win.Add_SourceInitialized({
        $h = (New-Object System.Windows.Interop.WindowInteropHelper($win)).Handle
        $v = [int]$dark; [void][GmWin]::DwmSetWindowAttribute($h, 20, [ref]$v, 4)
        $c = [System.Windows.Media.ColorConverter]::ConvertFromString($p.bg)
        $cap = [int]($c.R + 256 * $c.G + 65536 * $c.B); [void][GmWin]::DwmSetWindowAttribute($h, 35, [ref]$cap, 4)
    })

    & $updateSummary

    if ($shotPath) {
        $win.Show(); $win.Activate()
        for ($i = 0; $i -lt 8; $i++) { $win.Dispatcher.Invoke([Action]{}, [System.Windows.Threading.DispatcherPriority]::Background); Start-Sleep -Milliseconds 150 }
        $h = (New-Object System.Windows.Interop.WindowInteropHelper($win)).Handle
        $r = New-Object GmWin+RECT; [void][GmWin]::GetWindowRect($h, [ref]$r)
        $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
        $bmp = New-Object System.Drawing.Bitmap($w, $ht)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
        $bmp.Save($shotPath, [System.Drawing.Imaging.ImageFormat]::Png)
        $g.Dispose(); $bmp.Dispose(); $win.Close()
        return [pscustomobject]@{ Confirmed = $true; Keys = @($items | ForEach-Object { $_.Key }) }
    }

    $res = $win.ShowDialog()
    $keys = @($checks | Where-Object { $_.IsChecked } | ForEach-Object { $_.Tag.Key })
    return [pscustomobject]@{ Confirmed = ($res -eq $true); Keys = $keys }
}
