//
//  AddSecretViewController.m
//  Mitro
//
//  Created by Aaron Sullivan on 10/30/16.
//  Copyright © 2016 Aquarious, Inc. All rights reserved.
//

#import "AddSecretViewController.h"

@interface AddSecretViewController ()

@end

@implementation AddSecretViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    // Do any additional setup after loading the view.
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];

    [[Mitro secretManager] setDelegate:nil];
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

- (IBAction)secretTypeChanged:(UISegmentedControl *)sender {

    BOOL loginType = (sender.selectedSegmentIndex == 0);

    self.urlField.hidden      = !loginType;
    self.usernameField.hidden = !loginType;
    self.passwordField.hidden = !loginType;
    self.noteView.hidden      = loginType;
}

- (IBAction)cancelTapped:(id)sender {
    if (self.delegate && [self.delegate respondsToSelector:@selector(didCancel)]) {
        [self.delegate didCancel];
    }
}

- (IBAction)saveTapped:(id)sender {

    [self.view setUserInteractionEnabled:NO];

    [[Mitro secretManager] setDelegate:self];
    if (self.secretTypeControl.selectedSegmentIndex == 0) {
        [[Mitro secretManager] addSecretLoginTitle:self.titleField.text
                                               url:self.urlField.text
                                          username:self.usernameField.text
                                          password:self.passwordField.text];
    } else {
        [[Mitro secretManager] addSecretNoteTitle:self.titleField.text note:self.noteView.text];
    }
}

- (void)onAddSecret {
    [self.view setUserInteractionEnabled:YES];

    if (self.delegate && [self.delegate respondsToSelector:@selector(didAddSecret)]) {
        [self.delegate didAddSecret];
    }
}

- (void)onAddSecretFailed:(NSError *)error {
    [self.view setUserInteractionEnabled:YES];

    UIAlertView *alert = [[UIAlertView alloc] initWithTitle:@"Error Adding Secret"
                                                    message:error.localizedDescription
                                                   delegate:nil
                                          cancelButtonTitle:@"OK"
                                          otherButtonTitles:nil];
    [alert show];
}

@end
