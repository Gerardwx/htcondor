from pathlib import Path

import htcondor2 as htcondor
import classad2 as classad

from htcondor_cli.noun import Noun
from htcondor_cli.verb import Verb


class List(Verb):
    # For now, this only works for the authenticated identity and the
    # various different local daemons (depending on the command).
    def __init__(self, logger, **options):
        user = None
        credd = htcondor.Credd()

        # Check for a password credential.
        has_windows_password = False
        windows_password_time = None
        try:
            # This ignores the specified daemon and asks the schedd directly;
            # the schedd doesn't even register the corresponding command except
            # on Windows, so we get an exception instead of an answer.  Sigh.
            has_windows_password = credd.query_password(user)
            windows_password_time = credd.query_user_cred(htcondor.CredTypes.Password, user)
        except OSError:
            pass
        if windows_password_time is not None:
            print(f">> Windows password timestamp {windows_password_time}")
        else:
            print(f">> no Windows password found");

        # Check for Kerberos credential.
        kerberos_time = None
        try:
            # This throws an exception if the Kerberos credential directory
            # is misconfigured, which is super not-helpful, because if it's
            # configured but not active, the schedd blocks for ten minutes
            # on start-up.
            kerberos_time = credd.query_user_cred(htcondor.CredTypes.Kerberos, user)
        except OSError:
            pass
        if kerberos_time is not None:
            print(f">> Kerberos timestamp {kerberos_time}")
        else:
            print(f">> no Kerberos credential found")

        # Check for OAuth2 credentias.
        oauth_classad_string = credd.query_user_service_cred(htcondor.CredTypes.OAuth, None, None, user)

        if oauth_classad_string is None:
            print(f">> no OAuth credentials found")
            return

        ad = classad.parseOne(oauth_classad_string)
        names = {}
        for key in ad.keys():
            name = None
            if key.endswith(".top"):
                name = key[:-4]
            elif key.endswith(".use"):
                name = key[:-4]

            if name is not None:
                names[name] = ad[key]

        print(f">> Found OAuth2 credentials:")
        for name in names:
            print(f"   {name} @{names[name]}")


class Add(Verb):
    def _handle_add_password(*, credential_file, ** options):
        user = None
        credd = htcondor.Credd()
        contents = Path(credential_file).read_text()

        # This can't work except for on Windows...
        credd.add_user_cred(htcondor.CredTypes.Password, contents, user)


    def _handle_add_kerberos(*, credential_file, ** options):
        user = None
        credd = htcondor.Credd()
        contents = Path(credential_file).read_text()

        credd.add_user_cred(htcondor.CredTypes.Kerberos, contents, user)


    def _handle_add_oauth2(*, credential_file, service, handle, ** options):
        user = None
        credd = htcondor.Credd()
        contents = Path(credential_file).read_text()

        credd.add_user_service_cred(htcondor.CredTypes.OAuth, contents, service, handle, user)


    choices = {
        "password":     _handle_add_password,
        "kerberos":     _handle_add_kerberos,
        "oauth2":       _handle_add_oauth2,
    }


    options = {
        "type": {
            "args":         ("type",),
            "metavar":      "type",
            "choices":      choices.keys(),
            "help":         "The credential type: password, kerberos, or oauth2",
        },
        "credential-file": {
            "args":         ("credential_file",),
            "metavar":      "credential-file",
            "help":         "Path to a file storing the credential",
        },
        "service": {
            "args":         ("--service",),
            "metavar":      "service",
            "help":         "(OAuth2)  Service name, if not from the file",
        },
        "handle": {
            "args":         ("--handle",),
            "metavar":      "handle",
            "help":         "(OAuth2)  Handle name, if not from the file",
        },
    }


    def __init__(self, logger, **options):
        htcondor.enable_debug()
        self.choices[options['type']](** options)


class Remove(Verb):
    def __init__(self, logger, **options):
        pass


class Credential(Noun):
    '''
    Operations on credentials.
    '''

    class list(List):
        pass


    class add(Add):
        pass


    class remove(Remove):
        pass


    @classmethod
    def verbs(cls):
        return [cls.list, cls.add, cls.remove]
