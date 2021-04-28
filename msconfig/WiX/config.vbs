Function ReplaceConfig(configName, newValue, srcTxt)
  Set re = new RegExp
  re.Global = true
  re.Multiline = true
  re.IgnoreCase = true
  re.Pattern = "^" & configName & "[ \t]*=.+$"
  Set matches = re.Execute(srcTxt)
  if matches.Count = 0 then
    re.Pattern = "^#" & configName & "[ \t]*=.+$"
    Set matches = re.Execute(srcText)
    if matches.Count = 0 then
      ' can't just append new attrib because of possible $$, so append dummy, then search and replace it with real stuff.
      srcTxt = srcTxt & configName & " = _" & VbCrLf
      re.Pattern = "^" & configName & "[ \t]*=.+$"
      re.Execute(srcText)
    end if
  end if
  ReplaceConfig = re.Replace(srcTxt, configName & " = " & newValue)
End Function

Function ReplaceMetaConfig(metaCat, oldVals, newValue, srcTxt)
  Set re = new RegExp
  re.Global = true
  re.Multiline = true
  re.IgnoreCase = true
  re.Pattern = "^[ \t]*use[ \t]*" & metaCat & "[ \t]*:[ \t]*" & oldVals & "$"
  Set matches = re.Execute(srcTxt)
  if matches.Count = 0 then
    ' can't just append new attrib because of possible $$, so append dummy, then search and replace it with real stuff.
    srcTxt = srcTxt & "use " & metaCat & " : " & newValue & VbCrLf
    re.Execute(srcText)
  end if
  ReplaceMetaConfig = re.Replace(srcTxt, "use " & metaCat & " : " & newValue)
End Function

Function CreateConfig2()
  Set fso = CreateObject("Scripting.FileSystemObject")
  path = Session.Property("INSTALLDIR")
  Set installpath = fso.GetFolder(path)
  strippedPath = installpath.ShortPath

  cclpath = fso.BuildPath(path,"condor_config.local")
  if Not fso.FileExists(cclpath) Then
   Set lc = fso.CreateTextFile(cclpath)
   lc.WriteLine("## Customize the condor configuration here" & VbCrLf)
   lc.Close
  End If

  ccpath = fso.BuildPath(path,"condor_config")
  If Not fso.FileExists(ccpath) Then
    ccgpath = fso.BuildPath(path, "etc\condor_config.base")
  End if

  Set ConfigBase = fso.OpenTextFile(ccpath, 1, True)
  configTxt = ConfigBase.ReadAll
  configTxt = configTxt & VbCrLf
  ConfigBase.Close

  daemonList = "MASTER"
  configTxt = ReplaceConfig("RELEASE_DIR",strippedPath,configTxt)

  localConfig = Session.Property("LOCALCONFIG")
  if Left(localConfig, 4) = "http" Then
     localConfigCmd = "condor_urlfetch -$(SUBSYSTEM) " & localConfig & " $(LOCAL_DIR)\condor_config.url_cache |"
     configTxt = ReplaceConfig("LOCAL_CONFIG_FILE",localConfigCmd,configTxt)
  Else
     configTxt = ReplaceConfig("LOCAL_CONFIG_FILE",localConfig,configTxt)
  End If

  ' setup for recommended_v9_0 knob needs 2 or 3 args: condor, root@*, <other-admins>
  ' condor - bare user identity of daemons.  SYSTEM for services and $(USERNAME) for personal condor
  ' root
  daemonuser = Session.Property("DAEMONUSER")
  If daemonuser = "" Then daemonuser = "SYSTEM"

  installuser = Session.Property("LogonUser")
  If Not(installuser = "") And Not(installuser = "Administrator") Then
     configTxt = ReplaceConfig("INSTALL_USER",installuser,configTxt)
  Else
     installuser = ""
  End If

  propval = Session.Property("HOSTALLOWADMINISTRATOR")
  If propval = "" Then
     if daemonuser = "SYSTEM" Then
        propval = "Administrator@*"
     Else
        propval = "$(USERNAME)@*"
     End If
     if Not(installuser = "") Then propval = propval & ", $(INSTALL_USER)@*"
  End If

  propval = "recommended_v9_0(" & daemonuser & ", " & propval & ")"
  configTxt = ReplaceMetaConfig("SECURITY", "(HOST_BASED|USER_BASED|recommended_v9_0.*)", propval, configTxt)

  propval = Session.Property("HOSTALLOWREAD")
  if Not(propval = "") And Not(propval = "*") Then configTxt = ReplaceConfig("ALLOW_READ",propval,configTxt)
  propval = Session.Property("HOSTALLOWWRITE")
  if Not(propval = "") And Not(propval = "*") Then configTxt = ReplaceConfig("ALLOW_WRITE",propval,configTxt)

  Select Case Session.Property("NEWPOOL")
  Case "Y"
   configTxt = ReplaceConfig("CONDOR_HOST", "$(FULL_HOSTNAME)",configTxt)
   propval = Session.Property("POOLNAME")
   if Not(propval = "") Then configTxt = ReplaceConfig("COLLECTOR_NAME",propval,configTxt)
   daemonList = daemonList & " COLLECTOR NEGOTIATOR"
  Case "N"
   configTxt = ReplaceConfig("CONDOR_HOST",Session.Property("POOLHOSTNAME"),configTxt)
  End Select

  propval = Session.Property("ACCOUNTINGDOMAIN")
  if Not(propval = "") Then configTxt = ReplaceConfig("UID_DOMAIN",propval,configTxt)
  propval = Session.Property("CONDOREMAIL")
  if Not(propval = "") Then configTxt = ReplaceConfig("CONDOR_ADMIN",propval,configTxt)
  propval = Session.Property("SMTPSERVER")
  if Not(propval = "") Then configTxt = ReplaceConfig("SMTP_SERVER",propval,configTxt)

  jvmLoc = Session.Property("JVMLOCATION")
  if fso.FileExists(jvmLoc) then
   Set jvmPath = fso.GetFile(jvmLoc)
   jvmLoc = jvmPath.ShortPath
   configTxt = ReplaceConfig("JAVA",jvmLoc,configTxt)
  end if

  if Session.Property("SUBMITJOBS") = "Y" Then
   daemonList = daemonList & " SCHEDD"
  End If

  Select Case Session.Property("RUNJOBS")
  Case "A"
   configTxt = ReplaceMetaConfig("POLICY", "(DESKTOP|UWCS_DESKTOP|ALWAYS_RUN_JOBS)", "ALWAYS_RUN_JOBS", configTxt)
   'configTxt = ReplaceConfig("START","TRUE",configTxt)
   'configTxt = ReplaceConfig("SUSPEND","FALSE",configTxt)
   'configTxt = ReplaceConfig("WANT_SUSPEND","FALSE",configTxt)
   'configTxt = ReplaceConfig("WANT_VACATE","FALSE",configTxt)
   'configTxt = ReplaceConfig("PREEMPT","FALSE",configTxt)
   daemonList = daemonList & " STARTD"
  Case "N"
   'configTxt = ReplaceConfig("START","FALSE",configTxt)
  Case "I"
   configTxt = ReplaceMetaConfig("POLICY", "(DESKTOP|UWCS_DESKTOP|ALWAYS_RUN_JOBS)", "DESKTOP", configTxt)
   configTxt = ReplaceConfig("START","KeyboardIdle > $$(StartIdleTime)",configTxt)
   configTxt = ReplaceConfig("CONTINUE","KeyboardIdle > $$(ContinueIdleTime)",configTxt)
   daemonList = daemonList & " STARTD KBDD"
  Case "C"
   configTxt = ReplaceMetaConfig("POLICY", "(DESKTOP|UWCS_DESKTOP|ALWAYS_RUN_JOBS)", "DESKTOP", configTxt)
   daemonList = daemonList & " STARTD KBDD"
  End Select

  If Session.Property("VACATEJOBS") = "N" Then
   configTxt = ReplaceConfig("WANT_VACATE","FALSE",configTxt)
   configTxt = ReplaceConfig("WANT_SUSPEND","TRUE",configTxt)
  End If

  If Session.Property("USEVMUNIVERSE") = "Y" Then
   configTxt = ReplaceConfig("VM_TYPE","vmware",configTxt)
   configTxt = ReplaceConfig("VM_MAX_NUMBER",Session.Property("VMMAXNUMBER"),configTxt)
   configTxt = ReplaceConfig("VM_MEMORY",Session.Property("VMMEMORY"),configTxt)
   configTxt = ReplaceConfig("VMWARE_PERL",Session.Property("PERLLOCATION"),configTxt)
   Select Case Session.Property("VMNETWORKING")
   Case "A"
    configTxt = ReplaceConfig("VM_NETWORKING","TRUE",configTxt)
   Case "B"
    configTxt = ReplaceConfig("VM_NETWORKING","TRUE",configTxt)
    configTxt = ReplaceConfig("VM_NETWORKING_TYPE","bridge",configTxt)
   Case "C"
    configTxt = ReplaceConfig("VM_NETWORKING","TRUE",configTxt)
    configTxt = ReplaceConfig("VM_NETWORKING_TYPE","nat, bridge",configTxt)
   End Select
  End if

  configTxt = ReplaceConfig("DAEMON_LIST",daemonList,configTxt)

  ' enable these to make testing/debugging of install auth lists easier
  If Session.Property("DEBUG") = "Y" Then
    configTxt = ReplaceConfig("ALL_DEBUG","D_CAT",configTxt)
    configTxt = ReplaceConfig("TOOL_DEBUG","D_ALWAYS:2 D_SECURITY:1 D_COMMAND:1",configTxt)
    configTxt = ReplaceConfig("MASTER_DEBUG","D_ALWAYS:2 D_SECURITY:1 D_COMMAND:1",configTxt)
    configTxt = ReplaceConfig("COLLECTOR_DEBUG","D_ALWAYS:2 D_SECURITY:1 D_COMMAND:1",configTxt)
  End If

  Set Configfile = fso.OpenTextFile(ccpath, 2, True)
  Configfile.WriteLine configTxt
  Configfile.Close
End Function

CreateConfig2
