//
//  LoginViewController.m
//  Mitro
//
//  Created by Adam Hilss on 9/15/13.
//  Copyright (c) 2013 Lectorius, Inc. All rights reserved.
//

#import <QuartzCore/QuartzCore.h>

#import "LoginViewController.h"
#import "Mitro.h"
#import "MitroColor.h"

@interface LoginViewController ()

@end

@implementation LoginViewController

typedef NS_ENUM(NSInteger, LoginViewMode) {
    LoginViewModeUsername,
    LoginViewMode2FA,
    LoginViewModeSpinner
};


static const NSUInteger kHeaderBorderColor = 0xcccccc;

- (id)initWithNibName:(NSString *)nibNameOrNil bundle:(NSBundle *)nibBundleOrNil {
    self = [super initWithNibName:nibNameOrNil bundle:nibBundleOrNil];
    if (self) {
    }
    return self;
}

- (void)viewDidLoad {
    [super viewDidLoad];

    // Style signup button
    self.signUpButton.layer.cornerRadius = 5.0;
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    CALayer *bottomBorder = [CALayer layer];
    
    bottomBorder.frame = CGRectMake(0.0f, 83.0f, 320.0, 1.0f);
    bottomBorder.backgroundColor = [UIColor colorWithHex:kHeaderBorderColor].CGColor;
    
    [self.headerView.layer addSublayer:bottomBorder];

    [self.navigationController setNavigationBarHidden:YES animated:animated];

    [[Mitro sessionManager] setDelegate:self];
    NSString* username = [[Mitro sessionManager] savedUsername];

    if (username != nil) {
        self.usernameText.text = username;
    }

    self.usernameText.delegate = self;
    self.passwordText.delegate = self;
    self.verificationCodeText.delegate = self;

    // Auto login with saved credentials
    if ([[Mitro sessionManager] savedUsername] != nil &&
        [[Mitro sessionManager] savedEncryptedPrivateKey] != nil) {
        // Hide login UI
        [self configureViewWithMode:LoginViewModeSpinner];

        [self tryLogin];
    }
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [self.navigationController setNavigationBarHidden:NO animated:animated];

    [[Mitro sessionManager] setDelegate:nil];
}

- (void)tryLogin {
    if (self.loginActivityIndicator.isAnimating) {
        return;
    }
    [self.loginActivityIndicator startAnimating];

    NSString* username = [self.usernameText.text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSString* password = self.passwordText.text;
    NSString* code = self.verificationCodeText.text;

    [[Mitro sessionManager] setShouldKeepLoggedIn:[self.savePasswordSwitch isOn]];

    [[Mitro sessionManager] login:username withPassword:password withTwoFactorAuthCode:code];
}

- (IBAction)onSignInTouched:(id)sender {
    [self.view endEditing:YES];
    [self tryLogin];
}

- (IBAction)signUpTapped:(id)sender {
    NSString *message = @"Please sign up at www.vaultapp.xyz using the desktop version of Chrome.";
    UIAlertView *alert = [[UIAlertView alloc] initWithTitle:@"Sign Up"
                                                    message:message
                                                   delegate:nil
                                          cancelButtonTitle:@"OK"
                                          otherButtonTitles:nil];
    [alert show];
}

- (BOOL)textFieldShouldReturn:(UITextField *)textField {
    NSInteger nextTag = textField.tag + 1;
    UIResponder* nextResponder = [textField.superview viewWithTag:nextTag];
    if (nextResponder) {
        [nextResponder becomeFirstResponder];
    } else {
        [textField resignFirstResponder];
        [self tryLogin];
    }

    return NO;
}

- (void)onLogin {
    [self.loginActivityIndicator stopAnimating];
    [self.navigationController popViewControllerAnimated:YES];
}

- (void)configureViewWithMode:(LoginViewMode)mode {
    switch (mode) {
        case LoginViewMode2FA:
            // Configure for 2FA code
            self.usernameText.hidden = YES;
            self.passwordText.hidden = YES;
            self.savePasswordSwitch.hidden = YES;
            self.savePasswordLabel.hidden = YES;
            self.signInButton.hidden = YES;

            self.verificationCodeText.hidden = NO;
            self.verificationCodeLabel.hidden = NO;
            self.verifyButton.hidden = NO;
            break;

        case LoginViewModeUsername:
            // Default is just spinner
            self.usernameText.hidden = NO;
            self.passwordText.hidden = NO;
            self.savePasswordSwitch.hidden = NO;
            self.savePasswordLabel.hidden = NO;
            self.signInButton.hidden = NO;

            self.verificationCodeText.hidden = YES;
            self.verificationCodeLabel.hidden = YES;
            self.verifyButton.hidden = YES;
            break;

        case LoginViewModeSpinner:
        default:
            // Default is just spinner
            self.usernameText.hidden = YES;
            self.passwordText.hidden = YES;
            self.savePasswordSwitch.hidden = YES;
            self.savePasswordLabel.hidden = YES;
            self.signInButton.hidden = YES;

            self.verificationCodeText.hidden = YES;
            self.verificationCodeLabel.hidden = YES;
            self.verifyButton.hidden = YES;
            break;
    }
}

- (void)onLoginFailed:(NSError*)error {
    [self.loginActivityIndicator stopAnimating];

    NSString* exceptionType = [error.userInfo objectForKey:@"ExceptionType"];

    if ([exceptionType isEqualToString:@"DoTwoFactorAuthException"]) {
        if (self.verificationCodeText.hidden) {
            [self configureViewWithMode:LoginViewMode2FA];
            [self.verificationCodeText becomeFirstResponder];
        } else {
            UIAlertView *alert = [[UIAlertView alloc] initWithTitle:@"Invalid Code"
                                                            message:@""
                                                           delegate:nil
                                                  cancelButtonTitle:@"OK"
                                                  otherButtonTitles:nil];
            [alert show];
            [self.verificationCodeText becomeFirstResponder];
            [self.verificationCodeText selectAll:nil];
        }
    } else {
        NSString* alertTitle = nil;
        NSString* alertMessage = nil;

        if ([exceptionType isEqualToString:@"DoEmailVerificationException"]) {
            alertTitle = @"Verification Required";
            alertMessage = [NSString stringWithFormat:@"For security reasons, an email has been sent to %@ to verify your new device. "
                            "Click on the link in your email and try again.", self.usernameText.text];
        } else {
            alertTitle = @"Login Error";
            alertMessage = error.localizedDescription;

            if ([error.localizedDescription isEqualToString:@"Invalid password"] && self.passwordText.hidden) {
                [self configureViewWithMode:LoginViewModeUsername];
            }
        }
        UIAlertView *alert = [[UIAlertView alloc] initWithTitle:alertTitle
                                                        message:alertMessage
                                                       delegate:nil
                                              cancelButtonTitle:@"OK"
                                              otherButtonTitles:nil];
        [alert show];
        [self.passwordText becomeFirstResponder];
        [self.passwordText selectAll:nil];
    }
}

@end
